package main

import (
	"bytes"
	"context"
	"path/filepath"
	"strings"
	"testing"

	"haruhidb-go/haruhidb"
	"haruhidb-go/internal/nl"
)

type fixedTranslator struct {
	output nl.TranslateOutput
	err    error
}

func (f *fixedTranslator) Translate(context.Context, nl.TranslateInput) (nl.TranslateOutput, error) {
	if f.err != nil {
		return nl.TranslateOutput{}, f.err
	}
	return f.output, nil
}

func TestRunCommandWithInlineJSON(t *testing.T) {
	dbPath := prepareCLITestDB(t)

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := run([]string{
		"run",
		"--db-path", dbPath,
		"--json", `{"version":"v1","request_id":"req-cli-run","mode":"read_only","action":"list_tables","args":{}}`,
	}, strings.NewReader(""), &stdout, &stderr)
	if err != nil {
		t.Fatalf("run command failed: %v; stderr=%s", err, stderr.String())
	}
	if !strings.Contains(stdout.String(), `"ok": true`) {
		t.Fatalf("unexpected output: %s", stdout.String())
	}
}

func TestNLCommandPreviewAndExecute(t *testing.T) {
	dbPath := prepareCLITestDB(t)

	originalFactory := translatorFactory
	translatorFactory = func(commonOptions) (nl.Translator, error) {
		return &fixedTranslator{
			output: nl.TranslateOutput{
				Candidate: []byte(`{"version":"v1","request_id":"req-cli-nl","mode":"read_only","action":"list_tables","args":{}}`),
				Model:     "test-model",
			},
		}, nil
	}
	defer func() {
		translatorFactory = originalFactory
	}()

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := run([]string{
		"nl",
		"--db-path", dbPath,
		"--input", "列出所有表",
		"--mode", "read_only",
		"--execute",
	}, strings.NewReader(""), &stdout, &stderr)
	if err != nil {
		t.Fatalf("nl command failed: %v; stderr=%s", err, stderr.String())
	}
	if !strings.Contains(stdout.String(), `"valid": true`) {
		t.Fatalf("unexpected nl output: %s", stdout.String())
	}
	if !strings.Contains(stdout.String(), `"action": "list_tables"`) {
		t.Fatalf("expected executed action output: %s", stdout.String())
	}
}

func TestShellJSONPath(t *testing.T) {
	dbPath := prepareCLITestDB(t)

	input := strings.NewReader(":json {\"version\":\"v1\",\"request_id\":\"req-shell\",\"mode\":\"read_only\",\"action\":\"list_tables\",\"args\":{}}\n:quit\n")
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := run([]string{
		"shell",
		"--db-path", dbPath,
	}, input, &stdout, &stderr)
	if err != nil {
		t.Fatalf("shell command failed: %v; stderr=%s", err, stderr.String())
	}
	if !strings.Contains(stdout.String(), `"ok": true`) {
		t.Fatalf("unexpected shell output: %s", stdout.String())
	}
}

func prepareCLITestDB(t *testing.T) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "cli_test.db")
	db, err := haruhidb.Open(path, haruhidb.OpenOptions{})
	if err != nil {
		t.Fatalf("open db failed: %v", err)
	}
	if err := db.Close(); err != nil {
		t.Fatalf("close db failed: %v", err)
	}
	return path
}
