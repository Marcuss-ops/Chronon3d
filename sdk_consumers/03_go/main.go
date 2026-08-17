// sdk_consumers/03_go/main.go
//
// Minimal Go consumer.  Uses cgo to bind the C ABI through the installed
// chronon3d.pc (pkg-config), so it needs no hard-coded paths and no bundled
// C sources.  Renders one frame of the canonical RenderPlan and asserts the
// output is non-empty.

package main

/*
#cgo pkg-config: chronon3d
#include <chronon3d/c_api/chronon3d.h>
#include <stdlib.h>
*/
import "C"

import (
	"fmt"
	"os"
	"unsafe"
)

func main() {
	json, err := os.ReadFile("plan.json")
	if err != nil {
		fmt.Fprintln(os.Stderr, "cannot read plan.json:", err)
		os.Exit(1)
	}
	cjson := C.CString(string(json))
	defer C.free(unsafe.Pointer(cjson))

	var cfg C.chronon_engine_config
	cfg.struct_size = C.uint32_t(unsafe.Sizeof(cfg))
	cfg.abi_version = C.chronon_abi_version()

	var engine *C.chronon_engine
	var errInfo *C.chronon_error_info
	if st := C.chronon_engine_create_v2(&cfg, &engine, errInfo); st != C.CHRONON_OK || engine == nil {
		fmt.Fprintln(os.Stderr, "engine create failed:", C.GoString(C.chronon_status_name(st)))
		os.Exit(1)
	}
	defer C.chronon_engine_destroy(engine)

	var plan *C.chronon_plan
	if st := C.chronon_plan_compile_json_n(engine, cjson, C.uint64_t(len(json)), &plan); st != C.CHRONON_OK || plan == nil {
		fmt.Fprintln(os.Stderr, "plan compile failed:", C.GoString(C.chronon_status_name(st)))
		os.Exit(1)
	}
	defer C.chronon_plan_destroy(plan)

	var info C.chronon_frame_info
	if st := C.chronon_render_frame_into(engine, plan, 0, nil, 0, &info); st != C.CHRONON_ERROR_BUFFER_TOO_SMALL || info.size == 0 {
		fmt.Fprintln(os.Stderr, "size query failed:", C.GoString(C.chronon_status_name(st)))
		os.Exit(1)
	}

	buf := C.malloc(C.size_t(info.size))
	if buf == nil {
		fmt.Fprintln(os.Stderr, "malloc failed")
		os.Exit(1)
	}
	defer C.free(buf)

	if st := C.chronon_render_frame_into(engine, plan, 0, buf, info.size, &info); st != C.CHRONON_OK {
		fmt.Fprintln(os.Stderr, "render failed:", C.GoString(C.chronon_status_name(st)))
		os.Exit(1)
	}

	raw := unsafe.Slice((*byte)(buf), int(info.size))
	nonzero := false
	for _, b := range raw {
		if b != 0 {
			nonzero = true
			break
		}
	}
	if !nonzero {
		fmt.Fprintln(os.Stderr, "rendered frame is empty")
		os.Exit(1)
	}

	fmt.Printf("GO_CONSUMER_PASS %dx%d\n", info.width, info.height)
}
