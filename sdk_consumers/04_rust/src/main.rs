// sdk_consumers/04_rust/src/main.rs
//
// Minimal Rust consumer.  Binds the C ABI with hand-written FFI declarations
// (no bindgen, no third-party crates) and links only libchronon3d_c.so.
// The link search path is provided by build.rs.

use std::ffi::CStr;
use std::os::raw::{c_char, c_int, c_void};

const CHRONON_OK: c_int = 0;
const CHRONON_ERROR_BUFFER_TOO_SMALL: c_int = 9;

#[repr(C)]
struct ChrononEngineConfig {
    struct_size: u32,
    abi_version: u32,
    assets_root: *const c_char,
    flags: u32,
}

#[repr(C)]
struct ChrononErrorInfo {
    struct_size: u32,
    status: c_int,
    message: *const c_char,
    code: *const c_char,
    component: *const c_char,
    node_id: *const c_char,
    asset: *const c_char,
}

#[repr(C)]
struct ChrononFrameInfo {
    width: u32,
    height: u32,
    stride: u32,
    pixel_format: u32,
    size: u64,
}

#[link(name = "chronon3d_c")]
extern "C" {
    fn chronon_abi_version() -> u32;
    fn chronon_status_name(status: c_int) -> *const c_char;
    fn chronon_engine_create_v2(
        config: *const ChrononEngineConfig,
        out_engine: *mut *mut c_void,
        out_error: *mut ChrononErrorInfo,
    ) -> c_int;
    fn chronon_plan_compile_json_n(
        engine: *mut c_void,
        json: *const c_char,
        json_size: u64,
        out_plan: *mut *mut c_void,
    ) -> c_int;
    fn chronon_render_frame_into(
        engine: *mut c_void,
        plan: *const c_void,
        frame: u64,
        destination: *mut c_void,
        destination_size: u64,
        out_info: *mut ChrononFrameInfo,
    ) -> c_int;
    fn chronon_plan_destroy(plan: *mut c_void);
    fn chronon_engine_destroy(engine: *mut c_void);
}

fn status_name(status: c_int) -> String {
    unsafe {
        let ptr = chronon_status_name(status);
        if ptr.is_null() {
            "UNKNOWN".to_string()
        } else {
            CStr::from_ptr(ptr).to_string_lossy().into_owned()
        }
    }
}

fn main() {
    let json = std::fs::read("plan.json").expect("cannot read plan.json");
    let json_len = json.len();
    let cjson = std::ffi::CString::new(json).expect("plan.json contains a NUL byte");

    let cfg = ChrononEngineConfig {
        struct_size: std::mem::size_of::<ChrononEngineConfig>() as u32,
        abi_version: unsafe { chronon_abi_version() },
        assets_root: std::ptr::null(),
        flags: 0,
    };

    let mut engine: *mut c_void = std::ptr::null_mut();
    let mut err = ChrononErrorInfo {
        struct_size: std::mem::size_of::<ChrononErrorInfo>() as u32,
        status: 0,
        message: std::ptr::null(),
        code: std::ptr::null(),
        component: std::ptr::null(),
        node_id: std::ptr::null(),
        asset: std::ptr::null(),
    };

    unsafe {
        let st = chronon_engine_create_v2(&cfg, &mut engine, &mut err);
        if st != CHRONON_OK || engine.is_null() {
            eprintln!("engine create failed: {}", status_name(st));
            std::process::exit(1);
        }

        let mut plan: *mut c_void = std::ptr::null_mut();
        let st = chronon_plan_compile_json_n(
            engine,
            cjson.as_ptr(),
            json_len as u64,
            &mut plan,
        );
        if st != CHRONON_OK || plan.is_null() {
            eprintln!("plan compile failed: {}", status_name(st));
            chronon_engine_destroy(engine);
            std::process::exit(1);
        }

        let mut info = ChrononFrameInfo {
            width: 0,
            height: 0,
            stride: 0,
            pixel_format: 0,
            size: 0,
        };
        let st = chronon_render_frame_into(
            engine,
            plan as *const c_void,
            0,
            std::ptr::null_mut(),
            0,
            &mut info,
        );
        if st != CHRONON_ERROR_BUFFER_TOO_SMALL || info.size == 0 {
            eprintln!("size query failed: {}", status_name(st));
            chronon_plan_destroy(plan);
            chronon_engine_destroy(engine);
            std::process::exit(1);
        }

        let mut buffer = vec![0u8; info.size as usize];
        let st = chronon_render_frame_into(
            engine,
            plan as *const c_void,
            0,
            buffer.as_mut_ptr() as *mut c_void,
            info.size,
            &mut info,
        );
        if st != CHRONON_OK {
            eprintln!("render failed: {}", status_name(st));
            chronon_plan_destroy(plan);
            chronon_engine_destroy(engine);
            std::process::exit(1);
        }
        if !buffer.iter().any(|&byte| byte != 0) {
            eprintln!("rendered frame is empty");
            chronon_plan_destroy(plan);
            chronon_engine_destroy(engine);
            std::process::exit(1);
        }

        chronon_plan_destroy(plan);
        chronon_engine_destroy(engine);
        println!("RUST_CONSUMER_PASS {}x{}", info.width, info.height);
    }
}
