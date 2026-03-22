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
