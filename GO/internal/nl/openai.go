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
	if strings.TrimSpace(cfg.APIKey) == "" {
		return nil, errors.New("openai api key must not be empty")
	}

	baseURL := strings.TrimSpace(cfg.BaseURL)
	if baseURL == "" {
		baseURL = defaultOpenAIBaseURL
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
		apiKey:  cfg.APIKey,
		baseURL: strings.TrimRight(baseURL, "/"),
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
	httpReq.Header.Set("Authorization", "Bearer "+t.apiKey)
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

	if strings.HasPrefix(trimmed, "```") {
		trimmed = strings.TrimPrefix(trimmed, "```json")
		trimmed = strings.TrimPrefix(trimmed, "```")
		trimmed = strings.TrimSuffix(trimmed, "```")
		trimmed = strings.TrimSpace(trimmed)
	}

	if !strings.HasPrefix(trimmed, "{") {
		return nil, errors.New("openai content is not a JSON object")
	}

	var decoded any
	dec := json.NewDecoder(strings.NewReader(trimmed))
	dec.UseNumber()
	if err := dec.Decode(&decoded); err != nil {
		return nil, fmt.Errorf("openai content invalid json: %w", err)
	}
	return []byte(trimmed), nil
}
