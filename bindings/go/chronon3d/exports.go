package chronon3d

// Exported callbacks live in their own file: cgo forbids C definitions in the
// preamble of a file that uses //export.

/*
#include <stdint.h>
*/
import "C"

import (
	"runtime/cgo"
	"unsafe"
)

//export chrononGoProgress
func chrononGoProgress(user unsafe.Pointer, current, total C.uint64_t) {
	rc := cgo.Handle(*(*C.uintptr_t)(user)).Value().(*renderCtx)
	if rc.progress != nil {
		rc.progress(uint64(current), uint64(total))
	}
}

//export chrononGoIsCancelled
func chrononGoIsCancelled(user unsafe.Pointer) C.int {
	rc := cgo.Handle(*(*C.uintptr_t)(user)).Value().(*renderCtx)
	if rc.ctx.Err() != nil {
		return 1
	}
	return 0
}
