package main

import (
	"bytes"
	"context"
	"os"
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

func TestNormalizeLLMOptionsWithOllamaShortcut(t *testing.T) {
	opts := commonOptions{
		ollama: true,
	}
	if err := normalizeLLMOptions(&opts); err != nil {
		t.Fatalf("normalizeLLMOptions returned error: %v", err)
	}

	if opts.openAIBaseURL != defaultOllamaBaseURL {
		t.Fatalf("unexpected base url: %s", opts.openAIBaseURL)
	}
	if opts.openAIModel != defaultOllamaModel {
		t.Fatalf("unexpected model: %s", opts.openAIModel)
	}
}

func TestNormalizeLLMOptionsWithOllamaModel(t *testing.T) {
	opts := commonOptions{
		ollamaModel: "qwen2.5-coder:1.5b",
	}
	if err := normalizeLLMOptions(&opts); err != nil {
		t.Fatalf("normalizeLLMOptions returned error: %v", err)
	}

	if opts.openAIBaseURL != defaultOllamaBaseURL {
		t.Fatalf("unexpected base url: %s", opts.openAIBaseURL)
	}
	if opts.openAIModel != "qwen2.5-coder:1.5b" {
		t.Fatalf("unexpected model: %s", opts.openAIModel)
	}
}

func TestBuildTranslatorWithLocalBaseURLWithoutAPIKey(t *testing.T) {
	t.Setenv("OPENAI_API_KEY", "")
	translator, err := buildTranslator(commonOptions{
		openAIBaseURL: "http://127.0.0.1:11434",
		openAIModel:   "qwen2.5-coder:0.5b",
	})
	if err != nil {
		t.Fatalf("buildTranslator returned error: %v", err)
	}
	if translator == nil {
		t.Fatal("expected translator to be configured")
	}
}

func TestBuildTranslatorRequiresAPIKeyForDefaultOpenAIEndpoint(t *testing.T) {
	t.Setenv("OPENAI_API_KEY", "")
	translator, err := buildTranslator(commonOptions{
		openAIModel: "gpt-5-mini",
	})
	if err == nil {
		t.Fatal("expected error when using default OpenAI endpoint without API key")
	}
	if translator != nil {
		t.Fatal("expected nil translator on error")
	}
}

func TestBuildTranslatorRejectsInvalidReasoningEffort(t *testing.T) {
	t.Setenv("OPENAI_API_KEY", "")
	translator, err := buildTranslator(commonOptions{
		openAIBaseURL:   "http://127.0.0.1:11434",
		openAIModel:     "qwen2.5-coder:0.5b",
		reasoningEffort: "max",
	})
	if err == nil {
		t.Fatal("expected error for invalid reasoning effort")
	}
	if translator != nil {
		t.Fatal("expected nil translator on error")
	}
}

func TestBuildTranslatorReadsExamplesFile(t *testing.T) {
	t.Setenv("OPENAI_API_KEY", "")

	examplesPath := filepath.Join(t.TempDir(), "examples.txt")
	if err := os.WriteFile(examplesPath, []byte("example: use batch"), 0o644); err != nil {
		t.Fatalf("write examples file failed: %v", err)
	}

	translator, err := buildTranslator(commonOptions{
		openAIBaseURL: "http://127.0.0.1:11434",
		openAIModel:   "qwen2.5-coder:0.5b",
		examplesPath:  examplesPath,
	})
	if err != nil {
		t.Fatalf("buildTranslator returned error: %v", err)
	}
	if translator == nil {
		t.Fatal("expected translator to be configured")
	}
}

func TestNormalizeLLMOptionsCLIOverrideBackendNoneWithOllama(t *testing.T) {
	opts := commonOptions{
		llmBackend: llmBackendNone,
		ollama:     true,
	}
	if err := normalizeLLMOptions(&opts); err != nil {
		t.Fatalf("normalizeLLMOptions returned error: %v", err)
	}

	if opts.llmBackend != llmBackendOllama {
		t.Fatalf("expected backend to switch to ollama, got %q", opts.llmBackend)
	}
	if opts.openAIBaseURL != defaultOllamaBaseURL {
		t.Fatalf("unexpected base url: %s", opts.openAIBaseURL)
	}
	if opts.openAIModel != defaultOllamaModel {
		t.Fatalf("unexpected model: %s", opts.openAIModel)
	}
}

func TestRunCommandWithConfigFile(t *testing.T) {
	dbPath := prepareCLITestDB(t)
	configPath := filepath.Join(t.TempDir(), "haruhidb.json")
	configJSON := `{
  "common": {
    "db_path": "` + dbPath + `"
  }
}`
	if err := os.WriteFile(configPath, []byte(configJSON), 0o644); err != nil {
		t.Fatalf("write config failed: %v", err)
	}

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := run([]string{
		"run",
		"--config", configPath,
		"--json", `{"version":"v1","request_id":"req-cli-run-config","mode":"read_only","action":"list_tables","args":{}}`,
	}, strings.NewReader(""), &stdout, &stderr)
	if err != nil {
		t.Fatalf("run command with config failed: %v; stderr=%s", err, stderr.String())
	}
	if !strings.Contains(stdout.String(), `"ok": true`) {
		t.Fatalf("unexpected output: %s", stdout.String())
	}
}

func TestCLIFlagsOverrideConfigFile(t *testing.T) {
	dbPath := prepareCLITestDB(t)
	configPath := filepath.Join(t.TempDir(), "haruhidb.json")
	configJSON := `{
  "common": {
    "db_path": "/path/that/does/not/exist/db-file.db"
  }
}`
	if err := os.WriteFile(configPath, []byte(configJSON), 0o644); err != nil {
		t.Fatalf("write config failed: %v", err)
	}

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := run([]string{
		"run",
		"--config", configPath,
		"--db-path", dbPath,
		"--json", `{"version":"v1","request_id":"req-cli-run-override","mode":"read_only","action":"list_tables","args":{}}`,
	}, strings.NewReader(""), &stdout, &stderr)
	if err != nil {
		t.Fatalf("run command with overridden db path failed: %v; stderr=%s", err, stderr.String())
	}
	if !strings.Contains(stdout.String(), `"ok": true`) {
		t.Fatalf("unexpected output: %s", stdout.String())
	}
}

func TestRunCommandUsesConfigRunJSON(t *testing.T) {
	dbPath := prepareCLITestDB(t)
	configPath := filepath.Join(t.TempDir(), "haruhidb.json")
	configJSON := `{
  "common": {
    "db_path": "` + dbPath + `"
  },
  "run": {
    "json": "{\"version\":\"v1\",\"request_id\":\"req-cli-run-config-json\",\"mode\":\"read_only\",\"action\":\"list_tables\",\"args\":{}}"
  }
}`
	if err := os.WriteFile(configPath, []byte(configJSON), 0o644); err != nil {
		t.Fatalf("write config failed: %v", err)
	}

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := run([]string{
		"run",
		"--config", configPath,
	}, strings.NewReader(""), &stdout, &stderr)
	if err != nil {
		t.Fatalf("run command with config run.json failed: %v; stderr=%s", err, stderr.String())
	}
	if !strings.Contains(stdout.String(), `"ok": true`) {
		t.Fatalf("unexpected output: %s", stdout.String())
	}
}

func TestRunCommandUsesConfigRunInputFile(t *testing.T) {
	dbPath := prepareCLITestDB(t)
	inputPath := filepath.Join(t.TempDir(), "request.json")
	if err := os.WriteFile(inputPath, []byte(`{"version":"v1","request_id":"req-cli-run-config-input","mode":"read_only","action":"list_tables","args":{}}`), 0o644); err != nil {
		t.Fatalf("write input file failed: %v", err)
	}

	configPath := filepath.Join(t.TempDir(), "haruhidb.json")
	configJSON := `{
  "common": {
    "db_path": "` + dbPath + `"
  },
  "run": {
    "input": "` + inputPath + `"
  }
}`
	if err := os.WriteFile(configPath, []byte(configJSON), 0o644); err != nil {
		t.Fatalf("write config failed: %v", err)
	}

	var stdout bytes.Buffer
	var stderr bytes.Buffer
	err := run([]string{
		"run",
		"--config", configPath,
	}, strings.NewReader(""), &stdout, &stderr)
	if err != nil {
		t.Fatalf("run command with config run.input failed: %v; stderr=%s", err, stderr.String())
	}
	if !strings.Contains(stdout.String(), `"ok": true`) {
		t.Fatalf("unexpected output: %s", stdout.String())
	}
}

func TestNLCommandUsesConfigNLInput(t *testing.T) {
	dbPath := prepareCLITestDB(t)
	configPath := filepath.Join(t.TempDir(), "haruhidb.json")
	configJSON := `{
  "common": {
    "db_path": "` + dbPath + `"
  },
  "nl": {
    "input": "列出所有表",
    "mode": "read_only"
  }
}`
	if err := os.WriteFile(configPath, []byte(configJSON), 0o644); err != nil {
		t.Fatalf("write config failed: %v", err)
	}

	originalFactory := translatorFactory
	translatorFactory = func(commonOptions) (nl.Translator, error) {
		return &fixedTranslator{
			output: nl.TranslateOutput{
				Candidate: []byte(`{"version":"v1","request_id":"req-cli-nl-config-input","mode":"read_only","action":"list_tables","args":{}}`),
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
		"--config", configPath,
	}, strings.NewReader(""), &stdout, &stderr)
	if err != nil {
		t.Fatalf("nl command with config nl.input failed: %v; stderr=%s", err, stderr.String())
	}
	if !strings.Contains(stdout.String(), `"valid": true`) {
		t.Fatalf("unexpected nl output: %s", stdout.String())
	}
}

func TestNLCommandUsesConfigNLInputFile(t *testing.T) {
	dbPath := prepareCLITestDB(t)
	inputPath := filepath.Join(t.TempDir(), "nl-input.txt")
	if err := os.WriteFile(inputPath, []byte("列出所有表"), 0o644); err != nil {
		t.Fatalf("write nl input file failed: %v", err)
	}

	configPath := filepath.Join(t.TempDir(), "haruhidb.json")
	configJSON := `{
  "common": {
    "db_path": "` + dbPath + `"
  },
  "nl": {
    "input_file": "` + inputPath + `",
    "mode": "read_only"
  }
}`
	if err := os.WriteFile(configPath, []byte(configJSON), 0o644); err != nil {
		t.Fatalf("write config failed: %v", err)
	}

	originalFactory := translatorFactory
	translatorFactory = func(commonOptions) (nl.Translator, error) {
		return &fixedTranslator{
			output: nl.TranslateOutput{
				Candidate: []byte(`{"version":"v1","request_id":"req-cli-nl-config-input-file","mode":"read_only","action":"list_tables","args":{}}`),
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
		"--config", configPath,
	}, strings.NewReader(""), &stdout, &stderr)
	if err != nil {
		t.Fatalf("nl command with config nl.input_file failed: %v; stderr=%s", err, stderr.String())
	}
	if !strings.Contains(stdout.String(), `"valid": true`) {
		t.Fatalf("unexpected nl output: %s", stdout.String())
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
