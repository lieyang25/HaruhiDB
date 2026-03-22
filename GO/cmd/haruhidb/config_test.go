package main

import "testing"

func TestDiscoverConfigPathFromArgs(t *testing.T) {
	t.Setenv(defaultConfigEnvKey, "")
	path, err := discoverConfigPath([]string{"--config", "/tmp/a.json"})
	if err != nil {
		t.Fatalf("discoverConfigPath returned error: %v", err)
	}
	if path != "/tmp/a.json" {
		t.Fatalf("unexpected path: %q", path)
	}
}

func TestDiscoverConfigPathUsesEnvFallback(t *testing.T) {
	t.Setenv(defaultConfigEnvKey, "/tmp/env.json")
	path, err := discoverConfigPath([]string{"run"})
	if err != nil {
		t.Fatalf("discoverConfigPath returned error: %v", err)
	}
	if path != "/tmp/env.json" {
		t.Fatalf("unexpected path: %q", path)
	}
}

func TestDiscoverConfigPathMissingValue(t *testing.T) {
	t.Setenv(defaultConfigEnvKey, "")
	_, err := discoverConfigPath([]string{"--config"})
	if err == nil {
		t.Fatal("expected missing value error")
	}
}
