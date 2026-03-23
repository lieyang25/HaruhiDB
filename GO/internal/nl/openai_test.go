package nl

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestNewOpenAITranslatorRequiresAPIKeyForDefaultEndpoint(t *testing.T) {
	translator, err := NewOpenAITranslator(OpenAIConfig{})
	if err == nil {
		t.Fatal("expected error when api key is missing for default endpoint")
	}
	if translator != nil {
		t.Fatal("expected nil translator on error")
	}
}

func TestNewOpenAITranslatorAllowsCustomEndpointWithoutAPIKey(t *testing.T) {
	translator, err := NewOpenAITranslator(OpenAIConfig{
		BaseURL: "http://127.0.0.1:11434",
		Model:   "qwen2.5-coder:0.5b",
	})
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if translator == nil {
		t.Fatal("expected translator to be created")
	}
}

func TestTranslateWithoutAPIKeySkipsAuthorizationHeader(t *testing.T) {
	var gotAuth string
	var gotPath string

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotAuth = r.Header.Get("Authorization")
		gotPath = r.URL.Path
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{
			"id":"resp-1",
			"model":"qwen2.5-coder:0.5b",
			"choices":[
				{
					"message":{
						"content":"{\"version\":\"v1\",\"request_id\":\"req-test\",\"mode\":\"read_only\",\"action\":\"list_tables\",\"args\":{}}"
					}
				}
			]
		}`))
	}))
	defer server.Close()

	translator, err := NewOpenAITranslator(OpenAIConfig{
		BaseURL: server.URL,
		Model:   "qwen2.5-coder:0.5b",
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
	if gotAuth != "" {
		t.Fatalf("expected no Authorization header, got %q", gotAuth)
	}
}

func TestExtractJSONPayload(t *testing.T) {
	valid := `{"version":"v1","request_id":"req-1","mode":"read_only","action":"list_tables","args":{}}`
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
			name:    "fenced json with surrounding text",
			content: "I converted your request:\n```json\n" + valid + "\n```\nDone.",
			want:    valid,
		},
		{
			name:    "inline text and json object",
			content: "Output: " + valid + " Thanks.",
			want:    valid,
		},
		{
			name:    "multiple json objects chooses protocol envelope",
			content: "debug: {\"note\":\"tmp\"}\nresult: " + valid,
			want:    valid,
		},
		{
			name:    "nested wrapper object chooses inner protocol envelope",
			content: "raw: {\"note\":\"tmp\",\"candidate\":" + valid + "}\nend",
			want:    valid,
		},
		{
			name:    "code fences with extra object chooses protocol envelope",
			content: "```json\n{\"note\":\"tmp\"}\n```\n```json\n" + valid + "\n```",
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
