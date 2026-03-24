package nl

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"haruhidb-go/internal/action"
)

func TestNewOllamaTranslatorAppliesDefaults(t *testing.T) {
	translator, err := NewOllamaTranslator(OllamaConfig{})
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if translator == nil {
		t.Fatal("expected translator to be created")
	}
	if translator.baseURL != defaultOllamaBaseURL {
		t.Fatalf("unexpected default base url: %s", translator.baseURL)
	}
	if translator.model != defaultOllamaModel {
		t.Fatalf("unexpected default model: %s", translator.model)
	}
}

func TestTranslateUsesChatCompletionsPath(t *testing.T) {
	var gotPath string

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath = r.URL.Path
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{
			"id":"resp-1",
			"model":"qwen2.5-coder:3b",
			"choices":[
				{
					"message":{
							"content":"{\"version\":\"v3\",\"request_id\":\"req-test\",\"mode\":\"read_only\",\"action\":\"list_tables\",\"args\":{}}"
					}
				}
			]
		}`))
	}))
	defer server.Close()

	translator, err := NewOllamaTranslator(OllamaConfig{
		BaseURL: server.URL,
		Model:   "qwen2.5-coder:3b",
	})
	if err != nil {
		t.Fatalf("unexpected translator init error: %v", err)
	}

	_, err = translator.Translate(context.Background(), TranslateInput{
		RequestID:      "req-test",
		NaturalRequest: "list tables",
	})
	if err != nil {
		t.Fatalf("translate failed: %v", err)
	}

	if gotPath != "/v1/chat/completions" {
		t.Fatalf("unexpected request path: %s", gotPath)
	}
}

func TestTranslateStreamingModeParsesSSEChunks(t *testing.T) {
	var gotStream bool

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		raw, err := io.ReadAll(r.Body)
		if err != nil {
			t.Fatalf("read request failed: %v", err)
		}
		var payload map[string]any
		if err := json.Unmarshal(raw, &payload); err != nil {
			t.Fatalf("decode request failed: %v", err)
		}
		if value, ok := payload["stream"].(bool); ok {
			gotStream = value
		}

		w.Header().Set("Content-Type", "text/event-stream")
		_, _ = w.Write([]byte("data: {\"id\":\"resp-stream\",\"model\":\"qwen2.5-coder:3b\",\"choices\":[{\"delta\":{\"reasoning\":\"thinking\"}}]}\n\n"))
		_, _ = w.Write([]byte("data: {\"id\":\"resp-stream\",\"model\":\"qwen2.5-coder:3b\",\"choices\":[{\"delta\":{\"content\":\"{\\\"version\\\":\\\"v3\\\",\\\"request_id\\\":\\\"req-stream\\\"\"}}]}\n\n"))
		_, _ = w.Write([]byte("data: {\"id\":\"resp-stream\",\"model\":\"qwen2.5-coder:3b\",\"choices\":[{\"delta\":{\"content\":\",\\\"mode\\\":\\\"read_only\\\",\\\"action\\\":\\\"list_tables\\\",\\\"args\\\":{}}\"}}]}\n\n"))
		_, _ = w.Write([]byte("data: [DONE]\n\n"))
	}))
	defer server.Close()

	translator, err := NewOllamaTranslator(OllamaConfig{
		BaseURL: server.URL,
		Model:   "qwen2.5-coder:3b",
		Stream:  true,
	})
	if err != nil {
		t.Fatalf("unexpected translator init error: %v", err)
	}

	output, err := translator.Translate(context.Background(), TranslateInput{
		RequestID:      "req-stream",
		NaturalRequest: "list tables",
	})
	if err != nil {
		t.Fatalf("translate failed: %v", err)
	}
	if !gotStream {
		t.Fatal("expected stream=true in request payload")
	}

	want := `{"version":"v3","request_id":"req-stream","mode":"read_only","action":"list_tables","args":{}}`
	if string(output.Candidate) != want {
		t.Fatalf("unexpected candidate: got %s, want %s", string(output.Candidate), want)
	}
}

func TestExtractJSONPayload(t *testing.T) {
	valid := `{"version":"v3","request_id":"req-1","mode":"read_only","action":"list_tables","args":{}}`
	tests := []struct {
		name    string
		content string
		want    string
		wantErr bool
	}{
		{
			name:    "plain object",
			content: valid,
			want:    valid,
		},
		{
			name:    "fenced json object",
			content: "```json\n" + valid + "\n```",
			want:    valid,
		},
		{
			name:    "multiple objects choose protocol envelope",
			content: "debug: {\"note\":\"tmp\"}\nresult: " + valid,
			want:    valid,
		},
		{
			name:    "top-level array is invalid",
			content: `["not","object"]`,
			wantErr: true,
		},
		{
			name:    "non-json text",
			content: "hello world",
			wantErr: true,
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got, err := extractJSONPayload(tc.content)
			if tc.wantErr {
				if err == nil {
					t.Fatal("expected error, got nil")
				}
				return
			}
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if string(got) != tc.want {
				t.Fatalf("unexpected payload: got %s, want %s", string(got), tc.want)
			}
		})
	}
}

func TestOllamaSystemPromptContainsCurrentActionSet(t *testing.T) {
	prompt := ollamaSystemPrompt()

	checks := []string{
		"\"version\" should be \"v3\"",
		"create_table",
		"drop_index",
		"batch",
		"batch is recommended",
	}
	for _, item := range checks {
		if !strings.Contains(prompt, item) {
			t.Fatalf("system prompt missing %q", item)
		}
	}
}

func TestBuildOllamaUserPromptIncludesModeSpecificActionList(t *testing.T) {
	readOnlyPrompt, err := buildOllamaUserPrompt(TranslateInput{
		RequestID:      "req-readonly",
		NaturalRequest: "列出所有表",
		Mode:           action.ModeReadOnly,
		Catalog:        CatalogSnapshot{},
	})
	if err != nil {
		t.Fatalf("build prompt failed: %v", err)
	}
	if !strings.Contains(readOnlyPrompt, "version_guideline") {
		t.Fatalf("expected version_guideline in prompt: %s", readOnlyPrompt)
	}
	if !strings.Contains(readOnlyPrompt, "batch_guideline") {
		t.Fatalf("expected batch_guideline in prompt: %s", readOnlyPrompt)
	}
	if !strings.Contains(readOnlyPrompt, "args_rule") {
		t.Fatalf("expected args_rule in prompt: %s", readOnlyPrompt)
	}
	if !strings.Contains(readOnlyPrompt, "mode: read_only") {
		t.Fatalf("expected prompt to include mode read_only: %s", readOnlyPrompt)
	}

	readWritePrompt, err := buildOllamaUserPrompt(TranslateInput{
		RequestID:      "req-readwrite",
		NaturalRequest: "创建表",
		Mode:           action.ModeReadWrite,
		Catalog:        CatalogSnapshot{},
	})
	if err != nil {
		t.Fatalf("build prompt failed: %v", err)
	}
	if !strings.Contains(readWritePrompt, "mode: read_write") {
		t.Fatalf("expected prompt to include mode read_write: %s", readWritePrompt)
	}
	if !strings.Contains(readWritePrompt, "guideline: prefer minimal actions") {
		t.Fatalf("expected relaxed action guideline in prompt: %s", readWritePrompt)
	}
}

func TestExtractJSONPayloadPrefersSupportedVersionAndAction(t *testing.T) {
	content := strings.Join([]string{
		"debug candidate: {\"version\":\"v9\",\"request_id\":\"bad\",\"mode\":\"read_only\",\"action\":\"list_tables\",\"args\":{}}",
		"final candidate: {\"version\":\"v3\",\"request_id\":\"req-v3\",\"mode\":\"read_write\",\"action\":\"create_table\",\"args\":{\"table\":\"books\",\"columns\":[{\"name\":\"id\",\"type\":\"INTEGER\",\"nullable\":false}]}}",
	}, "\n")

	got, err := extractJSONPayload(content)
	if err != nil {
		t.Fatalf("extract payload failed: %v", err)
	}

	if !strings.Contains(string(got), "\"version\":\"v3\"") || !strings.Contains(string(got), "\"action\":\"create_table\"") {
		t.Fatalf("unexpected selected candidate: %s", string(got))
	}
}

type streamErrorAfterFirstChunkReader struct {
	chunk []byte
	sent  bool
}

func (r *streamErrorAfterFirstChunkReader) Read(p []byte) (int, error) {
	if !r.sent {
		r.sent = true
		n := copy(p, r.chunk)
		return n, nil
	}
	return 0, context.DeadlineExceeded
}

func TestParseOllamaStreamReturnsCandidateWhenLateReadErrorOccurs(t *testing.T) {
	payload := "data: {\"id\":\"resp-stream\",\"model\":\"qwen2.5-coder:3b\",\"choices\":[{\"delta\":{\"content\":\"{\\\"version\\\":\\\"v3\\\",\\\"request_id\\\":\\\"req-stream\\\",\\\"mode\\\":\\\"read_only\\\",\\\"action\\\":\\\"list_tables\\\",\\\"args\\\":{}}\"}}]}\n\n"
	reader := &streamErrorAfterFirstChunkReader{chunk: []byte(payload)}

	_, _, _, candidate, err := parseOllamaStream(reader)
	if err != nil {
		t.Fatalf("parseOllamaStream failed: %v", err)
	}

	want := `{"version":"v3","request_id":"req-stream","mode":"read_only","action":"list_tables","args":{}}`
	if string(candidate) != want {
		t.Fatalf("unexpected candidate: got %s, want %s", string(candidate), want)
	}
}
