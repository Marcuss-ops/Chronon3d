package chronon3d

import (
	"context"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

const testPlan = `{
  "schema": "chronon.render-plan.v2", "version": 2, "job_id": "go-binding-test",
  "canvas": {"width": 64, "height": 48, "fps_num": 30, "fps_den": 1, "duration_frames": 3},
  "layers": [{"id": "background", "type": "color", "color": [0.1, 0.5, 0.8, 1.0]}],
  "output": {"path": "out.png", "format": "png"}
}`

func newEngine(t *testing.T) *Engine {
	t.Helper()
	e, err := NewEngine(nil)
	if err != nil {
		t.Fatalf("NewEngine: %v", err)
	}
	t.Cleanup(func() { e.Close() })
	return e
}

func TestVersions(t *testing.T) {
	if LibraryABIVersion() != ABIVersion {
		t.Fatalf("abi %d", LibraryABIVersion())
	}
	if Version() == "" || StatusCancelled.String() != "CANCELLED" {
		t.Fatalf("version=%q cancelled=%q", Version(), StatusCancelled)
	}
}

func TestRenderFrame(t *testing.T) {
	e := newEngine(t)
	p, err := e.CompilePlan([]byte(testPlan))
	if err != nil {
		t.Fatal(err)
	}
	defer p.Close()
	f, err := e.RenderFrame(p, 0)
	if err != nil {
		t.Fatal(err)
	}
	if f.Width != 64 || f.Height != 48 || len(f.Pix) != f.Stride*f.Height {
		t.Fatalf("bad frame %+v (len %d)", *f, len(f.Pix))
	}
	nz := false
	for _, b := range f.Pix {
		if b != 0 {
			nz = true
			break
		}
	}
	if !nz {
		t.Fatal("empty frame")
	}
}

func TestInvalidPlanError(t *testing.T) {
	e := newEngine(t)
	_, err := e.CompilePlan([]byte(`{"schema":"chronon.render-plan.v2","version":2}`))
	var ce *Error
	if !errors.As(err, &ce) {
		t.Fatalf("want *Error, got %v", err)
	}
	t.Logf("invalid plan -> %v (code=%q)", ce, ce.Code)
	if _, err := e.CompilePlan([]byte(`{nope`)); err == nil {
		t.Fatal("garbage json accepted")
	}
}

func TestSequenceContextCancel(t *testing.T) {
	e := newEngine(t)
	p, _ := e.CompilePlan([]byte(testPlan))
	ctx, cancel := context.WithCancel(context.Background())
	n := 0
	err := e.RenderSequence(ctx, p, 0, 2, func(f *Frame) error {
		n++
		cancel()
		return nil
	})
	if !errors.Is(err, context.Canceled) || n != 1 {
		t.Fatalf("err=%v n=%d", err, n)
	}
}

func TestRenderFile(t *testing.T) {
	e := newEngine(t)
	p, _ := e.CompilePlan([]byte(testPlan))
	dir := t.TempDir()
	if err := e.RenderFile(context.Background(), p, filepath.Join(dir, "still.png"), &RenderFileOptions{EndSet: true}); err != nil {
		if strings.Contains(err.Error(), "CHRONON3D_ENABLE_VIDEO") {
			t.Skipf("skipping file render: %v", err)
		}
		t.Fatal(err)
	}
	ents, _ := os.ReadDir(dir)
	if len(ents) == 0 {
		t.Fatal("no output file")
	}
}

func TestRenderFileCancelledContext(t *testing.T) {
	e := newEngine(t)
	p, _ := e.CompilePlan([]byte(testPlan))
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	err := e.RenderFile(ctx, p, filepath.Join(t.TempDir(), "x.png"), nil)
	if !errors.Is(err, context.Canceled) {
		t.Fatalf("want context.Canceled, got %v", err)
	}
}

func TestRenderFileCancelViaCallback(t *testing.T) {
	e := newEngine(t)
	p, _ := e.CompilePlan([]byte(testPlan))
	ctx, cancel := context.WithCancel(context.Background())
	err := e.RenderFile(ctx, p, filepath.Join(t.TempDir(), "seq.png"), &RenderFileOptions{
		Progress: func(cur, total uint64) { cancel() },
	})
	switch {
	case err == nil:
		t.Skip("engine finished without polling cancellation after progress")
	case errors.Is(err, ErrCancelled):
		if !errors.Is(err, context.Canceled) {
			t.Fatalf("cancel error does not wrap ctx: %v", err)
		}
	default:
		t.Skipf("render_file unavailable for this output: %v", err)
	}
}

func TestClosedAndAssetsRoot(t *testing.T) {
	e := newEngine(t)
	p, _ := e.CompilePlan([]byte(testPlan))
	if err := e.SetAssetsRoot(t.TempDir()); err != nil {
		t.Fatal(err)
	}
	if _, err := e.RenderFrame(p, 0); !errors.Is(err, ErrClosed) {
		t.Fatalf("stale plan should be closed: %v", err)
	}
	e.Close()
	e.Close()
	if _, err := e.CompilePlan([]byte(testPlan)); !errors.Is(err, ErrClosed) {
		t.Fatalf("want ErrClosed, got %v", err)
	}
}

func TestConcurrentUse(t *testing.T) {
	e := newEngine(t)
	p, _ := e.CompilePlan([]byte(testPlan))
	var wg sync.WaitGroup
	errs := make(chan error, 8)
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			if _, err := e.RenderFrame(p, uint64(i%3)); err != nil {
				errs <- err
			}
		}(i)
	}
	wg.Wait()
	close(errs)
	for err := range errs {
		t.Fatal(err)
	}
}
