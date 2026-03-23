package nl

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"

	"haruhidb-go/internal/action"
)

const (
	defaultOpenAIBaseURL  = "https://api.openai.com"
	defaultOpenAIModel    = "gpt-4.1-mini"
	maxExamplePromptRunes = 6000
)

type OpenAIConfig struct {
	APIKey          string
	BaseURL         string
	Model           string
	ReasoningEffort string
	PromptExamples  string
	Stream          bool
	HTTPClient      *http.Client
}

type OpenAITranslator struct {
	apiKey          string
	baseURL         string
	model           string
	reasoningEffort string
	promptExamples  string
	stream          bool
	client          *http.Client
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

	reasoningEffort, err := normalizeReasoningEffort(cfg.ReasoningEffort)
	if err != nil {
		return nil, err
	}
	promptExamples := normalizePromptExamples(cfg.PromptExamples)

	client := cfg.HTTPClient
	if client == nil {
		client = &http.Client{Timeout: 30 * time.Second}
	}

	return &OpenAITranslator{
		apiKey:          apiKey,
		baseURL:         baseURL,
		model:           model,
		reasoningEffort: reasoningEffort,
		promptExamples:  promptExamples,
		stream:          cfg.Stream,
		client:          client,
	}, nil
}

func (t *OpenAITranslator) Translate(ctx context.Context, in TranslateInput) (TranslateOutput, error) {
	if t == nil {
		return TranslateOutput{}, errors.New("translator is nil")
	}

	userPrompt, err := buildOpenAIUserPrompt(in, t.promptExamples)
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
	if t.stream {
		reqBody["stream"] = true
	}
	if t.reasoningEffort != "" && t.reasoningEffort != "off" {
		reqBody["reasoning_effort"] = t.reasoningEffort
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

	var (
		candidate []byte
		rawOutput string
		model     string
		meta      = map[string]any{
			"provider": "openai",
		}
	)

	if t.stream {
		if httpResp.StatusCode < 200 || httpResp.StatusCode >= 300 {
			respBytes, readErr := io.ReadAll(httpResp.Body)
			if readErr != nil {
				return TranslateOutput{}, fmt.Errorf("read openai response: %w", readErr)
			}
			return TranslateOutput{}, fmt.Errorf("openai api returned status %d: %s", httpResp.StatusCode, string(respBytes))
		}

		responseID, responseModel, usage, streamCandidate, streamRawOutput, streamErr := parseOpenAIStream(httpResp.Body)
		if streamErr != nil {
			return TranslateOutput{}, streamErr
		}
		candidate = streamCandidate
		rawOutput = streamRawOutput
		if strings.TrimSpace(responseModel) != "" {
			model = responseModel
		}
		if responseID != "" {
			meta["response_id"] = responseID
		}
		if len(usage) > 0 {
			meta["usage"] = usage
		}
	} else {
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

		rawOutput = parsed.Choices[0].Message.Content
		candidate, err = extractJSONPayload(rawOutput)
		if err != nil {
			return TranslateOutput{}, err
		}

		if parsed.ID != "" {
			meta["response_id"] = parsed.ID
		}
		if len(parsed.Usage) > 0 {
			meta["usage"] = parsed.Usage
		}
		if strings.TrimSpace(parsed.Model) != "" {
			model = parsed.Model
		}
	}

	if strings.TrimSpace(model) == "" {
		model = t.model
	}
	if shouldExposeRawModelOutput() && strings.TrimSpace(rawOutput) != "" {
		meta["raw_output"] = rawOutput
	}

	return TranslateOutput{
		Candidate: candidate,
		Model:     model,
		Meta:      meta,
	}, nil
}

func parseOpenAIStream(reader io.Reader) (responseID string, responseModel string, usage map[string]any, candidate []byte, rawOutput string, err error) {
	scanner := bufio.NewScanner(reader)
	scanner.Buffer(make([]byte, 0, 64*1024), 8*1024*1024)

	var contentBuilder strings.Builder

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		if strings.HasPrefix(line, ":") {
			continue
		}
		if strings.HasPrefix(line, "data:") {
			line = strings.TrimSpace(strings.TrimPrefix(line, "data:"))
		}
		if line == "" {
			continue
		}
		if line == "[DONE]" {
			break
		}

		var chunk struct {
			ID    string `json:"id"`
			Model string `json:"model"`
			Error *struct {
				Message string `json:"message"`
			} `json:"error"`
			Choices []struct {
				Delta struct {
					Content   string `json:"content"`
					Reasoning string `json:"reasoning"`
				} `json:"delta"`
				Message struct {
					Content   string `json:"content"`
					Reasoning string `json:"reasoning"`
				} `json:"message"`
			} `json:"choices"`
			Usage map[string]any `json:"usage"`
		}
		if err := json.Unmarshal([]byte(line), &chunk); err != nil {
			continue
		}
		if chunk.Error != nil && strings.TrimSpace(chunk.Error.Message) != "" {
			return "", "", nil, nil, "", fmt.Errorf("openai stream returned error: %s", chunk.Error.Message)
		}
		if responseID == "" {
			responseID = chunk.ID
		}
		if responseModel == "" {
			responseModel = chunk.Model
		}
		if len(chunk.Usage) > 0 {
			usage = chunk.Usage
		}

		for _, choice := range chunk.Choices {
			piece := choice.Delta.Content
			if piece == "" {
				piece = choice.Message.Content
			}
			if piece == "" {
				piece = choice.Delta.Reasoning
			}
			if piece == "" {
				piece = choice.Message.Reasoning
			}
			if piece == "" {
				continue
			}
			contentBuilder.WriteString(piece)
		}
	}

	if scanErr := scanner.Err(); scanErr != nil {
		return "", "", nil, nil, "", fmt.Errorf("read openai stream: %w", scanErr)
	}

	content := strings.TrimSpace(contentBuilder.String())
	if content == "" {
		return "", "", nil, nil, "", errors.New("openai stream has no content")
	}
	candidate, err = extractJSONPayload(content)
	if err != nil {
		return "", "", nil, nil, content, err
	}
	return responseID, responseModel, usage, candidate, content, nil
}

func normalizeReasoningEffort(raw string) (string, error) {
	effort := strings.ToLower(strings.TrimSpace(raw))
	switch effort {
	case "", "off", "none", "low", "medium", "high":
		if effort == "none" {
			return "off", nil
		}
		return effort, nil
	default:
		return "", fmt.Errorf("invalid reasoning effort %q: expected one of off/low/medium/high", raw)
	}
}

func normalizePromptExamples(raw string) string {
	trimmed := strings.TrimSpace(raw)
	if trimmed == "" {
		return ""
	}
	runes := []rune(trimmed)
	if len(runes) <= maxExamplePromptRunes {
		return trimmed
	}
	return string(runes[:maxExamplePromptRunes])
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

func buildOpenAIUserPrompt(in TranslateInput, promptExamples string) (string, error) {
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
	if strings.TrimSpace(promptExamples) != "" {
		parts = append(parts, fmt.Sprintf("action_examples_reference: %s", promptExamples))
	}

	return strings.Join(parts, "\n"), nil
}

func extractJSONPayload(content string) ([]byte, error) {
	trimmed := strings.TrimSpace(content)
	if trimmed == "" {
		return nil, errors.New("openai content is empty")
	}

	candidates := collectJSONCandidates(trimmed)
	if len(candidates) == 0 {
		return nil, errors.New("openai content does not contain a valid JSON object")
	}

	best, ok := selectBestJSONCandidate(candidates)
	if !ok {
		return nil, errors.New("openai content does not contain a valid JSON object")
	}
	return best, nil
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

func collectJSONCandidates(content string) [][]byte {
	candidates := make([][]byte, 0, 8)
	seen := make(map[string]struct{})

	add := func(candidate []byte) {
		candidate = bytes.TrimSpace(candidate)
		if len(candidate) == 0 {
			return
		}
		key := string(candidate)
		if _, exists := seen[key]; exists {
			return
		}
		seen[key] = struct{}{}
		copied := append([]byte(nil), candidate...)
		candidates = append(candidates, copied)
	}

	if candidate, ok := decodeJSONObjectStrict(content); ok {
		add(candidate)
	}

	for _, block := range extractJSONObjectFromCodeFences(content) {
		if candidate, ok := decodeJSONObjectStrict(block); ok {
			add(candidate)
		}
	}

	for _, candidate := range extractJSONObjectCandidates(content) {
		add(candidate)
	}

	return candidates
}

func extractJSONObjectFromCodeFences(content string) []string {
	blocks := make([]string, 0, 4)
	remaining := content
	for {
		start := strings.Index(remaining, "```")
		if start < 0 {
			return blocks
		}

		afterStart := remaining[start+3:]
		end := strings.Index(afterStart, "```")
		if end < 0 {
			return blocks
		}

		block := strings.TrimSpace(afterStart[:end])
		block = trimCodeFenceLanguage(block)
		blocks = append(blocks, block)

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

func extractJSONObjectCandidates(content string) [][]byte {
	candidates := make([][]byte, 0, 8)
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
		if offset <= 0 || offset > int64(len(segment)) {
			continue
		}

		candidate := strings.TrimSpace(segment[:offset])
		if strict, ok := decodeJSONObjectStrict(candidate); ok {
			candidates = append(candidates, strict)
		}
	}

	return candidates
}

func selectBestJSONCandidate(candidates [][]byte) ([]byte, bool) {
	bestIndex := -1
	bestScore := -1 << 30

	for i, candidate := range candidates {
		score := scoreJSONCandidate(candidate)
		if bestIndex == -1 || score > bestScore {
			bestIndex = i
			bestScore = score
			continue
		}
		if score == bestScore && len(candidate) < len(candidates[bestIndex]) {
			bestIndex = i
		}
	}

	if bestIndex < 0 {
		return nil, false
	}
	return candidates[bestIndex], true
}

func scoreJSONCandidate(raw []byte) int {
	var payload map[string]any
	dec := json.NewDecoder(bytes.NewReader(raw))
	dec.UseNumber()
	if err := dec.Decode(&payload); err != nil {
		return -1 << 30
	}

	requiredKeys := []string{"version", "request_id", "mode", "action", "args"}
	matchedKeys := 0
	score := 0
	for _, key := range requiredKeys {
		if _, ok := payload[key]; ok {
			matchedKeys++
			score += 2
		}
	}

	if value, ok := payload["version"].(string); ok {
		version := strings.TrimSpace(value)
		switch version {
		case action.VersionV1:
			score += 4
		case "":
		default:
			score++
		}
	}

	if value, ok := payload["request_id"].(string); ok && strings.TrimSpace(value) != "" {
		score += 2
	}

	if value, ok := payload["mode"].(string); ok {
		mode := action.Mode(strings.TrimSpace(value))
		if mode.Valid() {
			score += 4
		}
	}

	if value, ok := payload["action"].(string); ok {
		actionName := strings.TrimSpace(value)
		if actionName != "" {
			score += 6
			if action.Action(actionName).Valid() {
				score += 3
			}
		}
	}

	if args, exists := payload["args"]; exists {
		switch args.(type) {
		case map[string]any:
			score += 6
		case nil:
			score++
		default:
			score += 2
		}
	}

	if matchedKeys >= 3 {
		score += 3
	}
	if matchedKeys == len(requiredKeys) {
		score += 6
	}

	unknown := len(payload) - matchedKeys
	if unknown > 0 {
		score -= unknown
	}

	return score
}

func shouldExposeRawModelOutput() bool {
	switch strings.ToLower(strings.TrimSpace(os.Getenv("HARUHIDB_NL_DEBUG_RAW"))) {
	case "1", "true", "yes", "on":
		return true
	default:
		return false
	}
}
