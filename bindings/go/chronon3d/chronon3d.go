// Package chronon3d provides idiomatic Go bindings over the Chronon3D C ABI
// (<chronon/abi.h>, ABI major 2).
//
// By default the package links through pkg-config (`chronon3d.pc`, installed by
// the SDK). For an in-tree build, compile with `-tags chronon3d_manual` and set
// CGO_CFLAGS / CGO_LDFLAGS yourself (see README.md and scripts/run_binding_tests.sh,
// which derive them from CHRONON3D_LIB).
package chronon3d

/*
#include <chronon/abi.h>
#include <stdlib.h>
#include <stdint.h>

extern void chrononGoProgress(void* user, uint64_t current, uint64_t total);
extern int chrononGoIsCancelled(void* user);

static void chronon_go_progress_tramp(void* user, uint64_t c, uint64_t t) { chrononGoProgress(user, c, t); }
static int chronon_go_cancel_tramp(void* user) { return chrononGoIsCancelled(user); }

static void chronon_go_fill_callbacks(chronon_render_callbacks* cb, void* user) {
    cb->progress = chronon_go_progress_tramp;
    cb->is_cancelled = chronon_go_cancel_tramp;
    cb->user = user;
}
*/
import "C"

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"runtime"
	"runtime/cgo"
	"sync"
	"unsafe"
)

// ABIVersion is the C ABI major this package was written against.
const ABIVersion = 2

// LibraryABIVersion reports the ABI major of the linked library.
func LibraryABIVersion() uint32 { return uint32(C.chronon_abi_version()) }

// ContractVersion reports the contract-zone version of the linked library.
func ContractVersion() uint32 { return uint32(C.chronon_contract_version()) }

// Version reports the library version string.
func Version() string { return C.GoString(C.chronon_version_string()) }

// Options configures NewEngine.
type Options struct {
	// AssetsRoot is mounted for asset resolution. It can only be set at
	// creation time (C ABI limitation); see Engine.SetAssetsRoot.
	AssetsRoot string
}

// Engine is a Chronon3D engine instance. Methods are safe for concurrent use;
// calls are serialized internally (the C ABI rejects concurrent use with BUSY).
type Engine struct {
	mu         sync.Mutex
	h          *C.chronon_engine
	assetsRoot string
	plans      map[*Plan]struct{}
}

// NewEngine creates an engine. Call Close to release native resources.
func NewEngine(opts *Options) (*Engine, error) {
	if got := LibraryABIVersion(); got != ABIVersion {
		return nil, &Error{Status: StatusABIMismatch, Message: fmt.Sprintf("binding expects ABI %d, library reports %d", ABIVersion, got)}
	}
	e := &Engine{plans: map[*Plan]struct{}{}}
	if opts != nil {
		e.assetsRoot = opts.AssetsRoot
	}
	if err := e.create(); err != nil {
		return nil, err
	}
	runtime.SetFinalizer(e, func(e *Engine) { e.Close() })
	return e, nil
}

func (e *Engine) create() error {
	var cfg C.chronon_engine_config
	cfg.struct_size = C.uint32_t(unsafe.Sizeof(cfg))
	cfg.abi_version = C.chronon_abi_version()
	if e.assetsRoot != "" {
		cs := C.CString(e.assetsRoot)
		defer C.free(unsafe.Pointer(cs))
		cfg.assets_root = cs
	}
	var info C.chronon_error_info
	info.struct_size = C.uint32_t(unsafe.Sizeof(info))
	var h *C.chronon_engine
	st := C.chronon_engine_create_v2(&cfg, &h, &info)
	if st != C.CHRONON_OK || h == nil {
		return errorFromInfo(Status(st), &info, "engine creation failed")
	}
	e.h = h
	return nil
}

// Close destroys the engine and every plan it compiled. Idempotent.
func (e *Engine) Close() error {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.closeLocked()
	return nil
}

func (e *Engine) closeLocked() {
	for p := range e.plans {
		p.destroyLocked()
	}
	if e.h != nil {
		C.chronon_engine_destroy(e.h)
		e.h = nil
	}
}

// AssetsRoot returns the configured assets root.
func (e *Engine) AssetsRoot() string { return e.assetsRoot }

// SetAssetsRoot recreates the native engine with a new assets root (the C ABI
// accepts it only at creation). Previously compiled plans are invalidated.
func (e *Engine) SetAssetsRoot(path string) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.closeLocked()
	e.assetsRoot = path
	return e.create()
}

func (e *Engine) lastError(st Status) error {
	if e.h == nil {
		return newError(st, "")
	}
	var info C.chronon_error_info
	info.struct_size = C.uint32_t(unsafe.Sizeof(info))
	if C.chronon_engine_last_error_info(e.h, &info) != C.CHRONON_OK || Status(info.status) != st {
		return newError(st, "")
	}
	return errorFromInfo(st, &info, "")
}

// ErrClosed is returned when using a closed Engine or Plan.
var ErrClosed = errors.New("chronon3d: use of closed engine or plan")

// Plan is a compiled RenderPlan, owned by the Engine that compiled it.
type Plan struct {
	e      *Engine
	h      *C.chronon_plan
	Canvas Canvas
}

// Canvas mirrors the canvas block of the RenderPlan (best-effort parse).
type Canvas struct {
	Width          int `json:"width"`
	Height         int `json:"height"`
	FPSNum         int `json:"fps_num"`
	FPSDen         int `json:"fps_den"`
	DurationFrames int `json:"duration_frames"`
}

// CompilePlan compiles RenderPlan JSON.
func (e *Engine) CompilePlan(planJSON []byte) (*Plan, error) {
	if len(planJSON) == 0 {
		return nil, newError(StatusInvalidArgument, "empty plan")
	}
	var doc struct {
		Canvas Canvas `json:"canvas"`
	}
	_ = json.Unmarshal(planJSON, &doc)
	if doc.Canvas.FPSNum == 0 {
		doc.Canvas.FPSNum, doc.Canvas.FPSDen = 30, 1
	}
	if doc.Canvas.FPSDen == 0 {
		doc.Canvas.FPSDen = 1
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.h == nil {
		return nil, ErrClosed
	}
	src := C.CBytes(planJSON)
	defer C.free(src)
	var ph *C.chronon_plan
	if st := Status(C.chronon_plan_compile_json_n(e.h, (*C.char)(src), C.uint64_t(len(planJSON)), &ph)); st != StatusOK {
		return nil, e.lastError(st)
	}
	p := &Plan{e: e, h: ph, Canvas: doc.Canvas}
	e.plans[p] = struct{}{}
	return p, nil
}

// CompilePlanFile reads and compiles a RenderPlan JSON file.
func (e *Engine) CompilePlanFile(path string) (*Plan, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	return e.CompilePlan(b)
}

// Close releases the plan. Idempotent.
func (p *Plan) Close() error {
	p.e.mu.Lock()
	defer p.e.mu.Unlock()
	p.destroyLocked()
	return nil
}

func (p *Plan) destroyLocked() {
	if p.h != nil {
		C.chronon_plan_destroy(p.h)
		p.h = nil
	}
	delete(p.e.plans, p)
}

// Frame is one rendered frame; Pix is Go-owned memory.
type Frame struct {
	Index       uint64
	Width       int
	Height      int
	Stride      int
	PixelFormat uint32
	Pix         []byte
}

// RenderFrame renders a single frame.
func (e *Engine) RenderFrame(p *Plan, frame uint64) (*Frame, error) {
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.h == nil || p == nil || p.h == nil || p.e != e {
		return nil, ErrClosed
	}
	var buf C.chronon_frame_buffer
	if st := Status(C.chronon_render_frame(e.h, p.h, C.uint64_t(frame), &buf)); st != StatusOK {
		return nil, e.lastError(st)
	}
	return &Frame{
		Index: frame, Width: int(buf.width), Height: int(buf.height), Stride: int(buf.stride),
		PixelFormat: uint32(buf.pixel_format),
		Pix:         C.GoBytes(buf.data, C.int(buf.size)),
	}, nil
}

// RenderSequence renders frames [start, end] calling fn for each; it stops on
// ctx cancellation (checked between frames) or when fn returns an error.
func (e *Engine) RenderSequence(ctx context.Context, p *Plan, start, end uint64, fn func(*Frame) error) error {
	for i := start; i <= end; i++ {
		if err := ctx.Err(); err != nil {
			return err
		}
		f, err := e.RenderFrame(p, i)
		if err != nil {
			return err
		}
		if err := fn(f); err != nil {
			return err
		}
	}
	return nil
}

// RenderFileOptions configures RenderFile. Zero values default to the plan canvas.
type RenderFileOptions struct {
	Start, End     uint64
	EndSet         bool // set true to honor End == 0
	FPSNum, FPSDen uint32
	Progress       func(current, total uint64)
}

type renderCtx struct {
	ctx      context.Context
	progress func(uint64, uint64)
}

// RenderFile renders to outputPath (format chosen by the engine from the
// extension). Cancelling ctx aborts the render via the ABI's is_cancelled
// callback; the returned error then wraps both ErrCancelled and ctx.Err().
func (e *Engine) RenderFile(ctx context.Context, p *Plan, outputPath string, opts *RenderFileOptions) error {
	if ctx == nil {
		ctx = context.Background()
	}
	var o RenderFileOptions
	if opts != nil {
		o = *opts
	}
	if p == nil {
		return ErrClosed
	}
	if !o.EndSet && o.End == 0 {
		if p.Canvas.DurationFrames > 0 {
			o.End = uint64(p.Canvas.DurationFrames - 1)
		}
		if o.End < o.Start {
			o.End = o.Start
		}
	}
	if o.FPSNum == 0 {
		o.FPSNum, o.FPSDen = uint32(p.Canvas.FPSNum), uint32(p.Canvas.FPSDen)
	}
	if o.FPSDen == 0 {
		o.FPSDen = 1
	}
	if err := ctx.Err(); err != nil {
		return err
	}

	h := cgo.NewHandle(&renderCtx{ctx: ctx, progress: o.Progress})
	defer h.Delete()
	user := (*C.uintptr_t)(C.malloc(C.size_t(unsafe.Sizeof(C.uintptr_t(0)))))
	defer C.free(unsafe.Pointer(user))
	*user = C.uintptr_t(h)
	cb := (*C.chronon_render_callbacks)(C.malloc(C.size_t(unsafe.Sizeof(C.chronon_render_callbacks{}))))
	defer C.free(unsafe.Pointer(cb))
	C.chronon_go_fill_callbacks(cb, unsafe.Pointer(user))

	path := C.CString(outputPath)
	defer C.free(unsafe.Pointer(path))

	e.mu.Lock()
	defer e.mu.Unlock()
	if e.h == nil || p.h == nil || p.e != e {
		return ErrClosed
	}
	st := Status(C.chronon_render_file(e.h, p.h, path, C.uint64_t(o.Start), C.uint64_t(o.End),
		C.uint32_t(o.FPSNum), C.uint32_t(o.FPSDen), cb))
	if st == StatusOK {
		return nil
	}
	err := e.lastError(st)
	if st == StatusCancelled && ctx.Err() != nil {
		return fmt.Errorf("%w: %w", err, ctx.Err())
	}
	return err
}
