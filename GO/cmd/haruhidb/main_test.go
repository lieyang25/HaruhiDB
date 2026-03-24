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

func TestRunServeRejectsRemovedModelFlags(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	err := run([]string{"serve", "--db-path", "tmp.db", "--model", "x"}, &stdout, &stderr)
	if err != nil {
		if !strings.Contains(err.Error(), "flag provided but not defined") {
			t.Fatalf("unexpected error: %v", err)
		}
		return
	}
	t.Fatal("expected unknown-flag error for removed --model")
}
