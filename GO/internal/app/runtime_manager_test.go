package app

import (
	"path/filepath"
	"testing"
	"time"
)

func TestRuntimeManagerResolveDefaultAndCustomPaths(t *testing.T) {
	dir := t.TempDir()
	defaultDBPath := filepath.Join(dir, "default.db")
	customDBPath := filepath.Join(dir, "custom.db")

	manager, err := NewRuntimeManager(RuntimeManagerConfig{
		DefaultDBPath:  defaultDBPath,
		AllowWrite:     true,
		RequestTimeout: 3 * time.Second,
	})
	if err != nil {
		t.Fatalf("NewRuntimeManager failed: %v", err)
	}
	defer func() {
		if closeErr := manager.Close(); closeErr != nil {
			t.Fatalf("close runtime manager failed: %v", closeErr)
		}
	}()

	defaultServiceA, resolvedDefaultA, err := manager.Resolve("")
	if err != nil {
		t.Fatalf("resolve default failed: %v", err)
	}
	if resolvedDefaultA == "" {
		t.Fatalf("expected resolved default db path")
	}

	defaultServiceB, resolvedDefaultB, err := manager.Resolve(defaultDBPath)
	if err != nil {
		t.Fatalf("resolve explicit default failed: %v", err)
	}
	if resolvedDefaultA != resolvedDefaultB {
		t.Fatalf("expected same resolved default path, got %q and %q", resolvedDefaultA, resolvedDefaultB)
	}
	if defaultServiceA != defaultServiceB {
		t.Fatalf("expected default path to reuse cached service instance")
	}

	customServiceA, resolvedCustomA, err := manager.Resolve(customDBPath)
	if err != nil {
		t.Fatalf("resolve custom failed: %v", err)
	}
	if resolvedCustomA == resolvedDefaultA {
		t.Fatalf("expected custom path to differ from default path")
	}
	customServiceB, resolvedCustomB, err := manager.Resolve(customDBPath)
	if err != nil {
		t.Fatalf("resolve custom second time failed: %v", err)
	}
	if resolvedCustomA != resolvedCustomB {
		t.Fatalf("expected same resolved custom path, got %q and %q", resolvedCustomA, resolvedCustomB)
	}
	if customServiceA != customServiceB {
		t.Fatalf("expected custom path to reuse cached service instance")
	}
}
