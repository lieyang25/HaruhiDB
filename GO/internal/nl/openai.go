package nl

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"haruhidb-go/internal/action"
)

const (
	defaultOpenAIBaseURL = "https://api.openai.com"
	defaultOpenAIModel   = "gpt-4.1-mini"
)

type OpenAIConfig struct {
	APIKey     string
	BaseURL    string
	Model      string
	HTTPClient *http.Client
}

type OpenAITranslator struct {
	apiKey  string
	baseURL string
	model   string
	client  *http.Client
}

func NewOpenAITranslator(cfg OpenAIConfig) (*OpenAITranslator, error) {
	baseURL := strings.TrimSpace(cfg.BaseURL)
	if baseURL == "" {
		baseURL = defaultOpenAIBaseURL
	}
	baseURL = strings.TrimRight(baseURL, "/")

	apiKey := strings.TrimSpace(cfg.APIKey)
	// API key is mandatory for the default OpenAI endpoint, but optional
	// for local OpenAI-compatible backends (for example Ollama).
	if apiKey == "" && strings.EqualFold(baseURL, defaultOpenAIBaseURL) {
		return nil, errors.New("openai api key must not be empty when using the default OpenAI endpoint")
	}

	model := strings.TrimSpace(cfg.Model)
	if model == "" {
		model = defaultOpenAIModel
	}

	client := cfg.HTTPClient
	if client == nil {
		client = &http.Client{Timeout: 30 * time.Second}
	}

	return &OpenAITranslator{
		apiKey:  apiKey,
		baseURL: baseURL,
		model:   model,
		client:  client,
	}, nil
}

func (t *OpenAITranslator) Translate(ctx context.Context, in TranslateInput) (TranslateOutput, error) {
	if t == nil {
		return TranslateOutput{}, errors.New("translator is nil")
	}

	userPrompt, err := buildOpenAIUserPrompt(in)
	if err != nil {
		return TranslateOutput{}, err
	}

	reqBody := map[string]any{
		"model":       t.model,
		"temperature": 0,
		"messages": []map[string]string{
			{
				"role":    "system",
				"content": openAISystemPrompt(),
			},
			{
				"role":    "user",
				"content": userPrompt,
			},
		},
		"response_format": map[string]string{
			"type": "json_object",
		},
	}

	bodyBytes, err := json.Marshal(reqBody)
	if err != nil {
		return TranslateOutput{}, fmt.Errorf("marshal openai request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(
		ctx,
		http.MethodPost,
		t.baseURL+"/v1/chat/completions",
		bytes.NewReader(bodyBytes),
	)
	if err != nil {
		return TranslateOutput{}, fmt.Errorf("create openai request: %w", err)
	}
	if t.apiKey != "" {
		httpReq.Header.Set("Authorization", "Bearer "+t.apiKey)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	httpResp, err := t.client.Do(httpReq)
	if err != nil {
		return TranslateOutput{}, fmt.Errorf("openai request failed: %w", err)
	}
	defer httpResp.Body.Close()

	respBytes, err := io.ReadAll(httpResp.Body)
	if err != nil {
		return TranslateOutput{}, fmt.Errorf("read openai response: %w", err)
	}

	if httpResp.StatusCode < 200 || httpResp.StatusCode >= 300 {
		return TranslateOutput{}, fmt.Errorf("openai api returned status %d: %s", httpResp.StatusCode, string(respBytes))
	}

	var parsed struct {
		ID      string `json:"id"`
		Model   string `json:"model"`
		Choices []struct {
			Message struct {
				Content string `json:"content"`
			} `json:"message"`
		} `json:"choices"`
		Usage map[string]any `json:"usage"`
	}
	if err := json.Unmarshal(respBytes, &parsed); err != nil {
		return TranslateOutput{}, fmt.Errorf("decode openai response: %w", err)
	}
	if len(parsed.Choices) == 0 {
		return TranslateOutput{}, errors.New("openai response has no choices")
	}

	candidate, err := extractJSONPayload(parsed.Choices[0].Message.Content)
	if err != nil {
		return TranslateOutput{}, err
	}

	meta := map[string]any{
		"provider": "openai",
	}
	if parsed.ID != "" {
		meta["response_id"] = parsed.ID
	}
	if len(parsed.Usage) > 0 {
		meta["usage"] = parsed.Usage
	}

	model := parsed.Model
	if strings.TrimSpace(model) == "" {
		model = t.model
	}

	return TranslateOutput{
		Candidate: candidate,
		Model:     model,
		Meta:      meta,
	}, nil
}

func openAISystemPrompt() string {
	return strings.TrimSpace(`
You are HaruhiDB action protocol translator.
Convert user natural-language requests into exactly one JSON object that matches HaruhiDB Action Protocol v1 request envelope.
Output MUST be valid JSON and must contain only these top-level keys:
"version", "request_id", "mode", "action", "args".
Do not include markdown fences.
Do not include explanation text.
Use action names and fields exactly as specified.
If request implies multiple steps, use action "batch" with "requests" list.
`)
}

func buildOpenAIUserPrompt(in TranslateInput) (string, error) {
	catalogJSON, err := json.Marshal(in.Catalog)
	if err != nil {
		return "", fmt.Errorf("marshal catalog snapshot: %w", err)
	}

	mode := in.Mode
	if mode == "" {
		mode = action.ModeReadOnly
	}

	parts := []string{
		fmt.Sprintf("request_id: %s", in.RequestID),
		fmt.Sprintf("mode: %s", mode),
		fmt.Sprintf("natural_request: %s", in.NaturalRequest),
		fmt.Sprintf("catalog_snapshot_json: %s", string(catalogJSON)),
	}

	if strings.TrimSpace(in.RepairHint) != "" {
		parts = append(parts, fmt.Sprintf("previous_output_error: %s", in.RepairHint))
		parts = append(parts, "Regenerate and strictly fix the error above.")
	}

	return strings.Join(parts, "\n"), nil
}

func extractJSONPayload(content string) ([]byte, error) {
	trimmed := strings.TrimSpace(content)
	if trimmed == "" {
		return nil, errors.New("openai content is empty")
	}

	if candidate, ok := decodeJSONObjectStrict(trimmed); ok {
		return candidate, nil
	}

	if candidate, ok := extractJSONObjectFromCodeFences(trimmed); ok {
		return candidate, nil
	}

	if candidate, ok := extractFirstJSONObject(trimmed); ok {
		return candidate, nil
	}

	return nil, errors.New("openai content does not contain a valid JSON object")
}

func decodeJSONObjectStrict(raw string) ([]byte, bool) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return nil, false
	}

	dec := json.NewDecoder(strings.NewReader(raw))
	dec.UseNumber()

	var decoded map[string]any
	if err := dec.Decode(&decoded); err != nil {
		return nil, false
	}

	var trailing any
	if err := dec.Decode(&trailing); err != io.EOF {
		return nil, false
	}

	return []byte(raw), true
}

func extractJSONObjectFromCodeFences(content string) ([]byte, bool) {
	remaining := content
	for {
		start := strings.Index(remaining, "```")
		if start < 0 {
			return nil, false
		}

		afterStart := remaining[start+3:]
		end := strings.Index(afterStart, "```")
		if end < 0 {
			return nil, false
		}

		block := strings.TrimSpace(afterStart[:end])
		block = trimCodeFenceLanguage(block)
		if candidate, ok := decodeJSONObjectStrict(block); ok {
			return candidate, true
		}

		remaining = afterStart[end+3:]
	}
}

func trimCodeFenceLanguage(block string) string {
	block = strings.TrimLeft(block, "\r\n")
	firstLine, rest, found := strings.Cut(block, "\n")
	if !found {
		return block
	}

	lang := strings.ToLower(strings.TrimSpace(firstLine))
	switch lang {
	case "json", "jsonc", "javascript", "js":
		return strings.TrimSpace(rest)
	default:
		return block
	}
}

func extractFirstJSONObject(content string) ([]byte, bool) {
	for i := 0; i < len(content); i++ {
		if content[i] != '{' {
			continue
		}

		segment := content[i:]
		dec := json.NewDecoder(strings.NewReader(segment))
		dec.UseNumber()

		var decoded map[string]any
		if err := dec.Decode(&decoded); err != nil {
			continue
		}

		offset := dec.InputOffset()
		if offset <= 0 {
			continue
		}

		candidate := strings.TrimSpace(segment[:offset])
		if strict, ok := decodeJSONObjectStrict(candidate); ok {
			return strict, true
		}
	}

	return nil, false
}
