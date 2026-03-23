package main

import (
	"bytes"
	"strings"
	"testing"
)

func TestRunRequiresSubcommand(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	err := run([]string{}, &stdout, &stderr)
	if err == nil {
		t.Fatal("expected missing subcommand error")
	}
	if !strings.Contains(err.Error(), "missing subcommand") {
		t.Fatalf("unexpected error: %v", err)
	}
}

func TestRunRejectsRemovedSubcommands(t *testing.T) {
	cases := []string{"run", "nl", "shell"}
	for _, sub := range cases {
		t.Run(sub, func(t *testing.T) {
			var stdout bytes.Buffer
			var stderr bytes.Buffer

			err := run([]string{sub}, &stdout, &stderr)
			if err == nil {
				t.Fatalf("expected removed subcommand error for %q", sub)
			}
			if !strings.Contains(err.Error(), "has been removed") {
				t.Fatalf("unexpected error for %q: %v", sub, err)
			}
		})
	}
}

func TestRunServeRequiresDBPath(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	err := run([]string{"serve"}, &stdout, &stderr)
	if err == nil {
		t.Fatal("expected db path error")
	}
	if !strings.Contains(err.Error(), "--db-path is required") {
		t.Fatalf("unexpected error: %v", err)
	}
}

func TestBuildTranslatorUsesDefaults(t *testing.T) {
	translator, err := buildTranslator(defaultServeOptions())
	if err != nil {
		t.Fatalf("buildTranslator returned error: %v", err)
	}
	if translator == nil {
		t.Fatal("expected translator")
	}
}
