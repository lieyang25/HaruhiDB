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
	"strings"
	"time"

	"haruhidb-go/internal/action"
)

const (
	defaultOllamaBaseURL = "http://127.0.0.1:11434"
	defaultOllamaModel   = "qwen2.5-coder:3b"
)

type OllamaConfig struct {
	BaseURL    string
	Model      string
	Stream     bool
	HTTPClient *http.Client
}

type OllamaTranslator struct {
	baseURL string
	model   string
	stream  bool
	client  *http.Client
}

func NewOllamaTranslator(cfg OllamaConfig) (*OllamaTranslator, error) {
	baseURL := strings.TrimSpace(cfg.BaseURL)
	if baseURL == "" {
		baseURL = defaultOllamaBaseURL
	}
	baseURL = strings.TrimRight(baseURL, "/")

	model := strings.TrimSpace(cfg.Model)
	if model == "" {
		model = defaultOllamaModel
	}

	client := cfg.HTTPClient
	if client == nil {
		client = &http.Client{Timeout: 60 * time.Second}
	}

	return &OllamaTranslator{
		baseURL: baseURL,
		model:   model,
		stream:  cfg.Stream,
		client:  client,
	}, nil
}

func (t *OllamaTranslator) Translate(ctx context.Context, in TranslateInput) (TranslateOutput, error) {
	if t == nil {
		return TranslateOutput{}, errors.New("translator is nil")
	}

	userPrompt, err := buildOllamaUserPrompt(in)
	if err != nil {
		return TranslateOutput{}, err
	}

	reqBody := map[string]any{
		"model":       t.model,
		"temperature": 0,
		"messages": []map[string]string{
			{
				"role":    "system",
				"content": ollamaSystemPrompt(),
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

	bodyBytes, err := json.Marshal(reqBody)
	if err != nil {
		return TranslateOutput{}, fmt.Errorf("marshal ollama request: %w", err)
	}

	httpReq, err := http.NewRequestWithContext(
		ctx,
		http.MethodPost,
		t.baseURL+"/v1/chat/completions",
		bytes.NewReader(bodyBytes),
	)
	if err != nil {
		return TranslateOutput{}, fmt.Errorf("create ollama request: %w", err)
	}
	httpReq.Header.Set("Content-Type", "application/json")

	httpResp, err := t.client.Do(httpReq)
	if err != nil {
		return TranslateOutput{}, fmt.Errorf("ollama request failed: %w", err)
	}
	defer httpResp.Body.Close()

	var (
		candidate []byte
		model     string
		meta      = map[string]any{
			"provider": "ollama",
		}
	)

	if t.stream {
		if httpResp.StatusCode < 200 || httpResp.StatusCode >= 300 {
			respBytes, readErr := io.ReadAll(httpResp.Body)
			if readErr != nil {
				return TranslateOutput{}, fmt.Errorf("read ollama response: %w", readErr)
			}
			return TranslateOutput{}, fmt.Errorf("ollama api returned status %d: %s", httpResp.StatusCode, string(respBytes))
		}

		responseID, responseModel, usage, streamCandidate, streamErr := parseOllamaStream(httpResp.Body)
		if streamErr != nil {
			return TranslateOutput{}, streamErr
		}
		candidate = streamCandidate
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
			return TranslateOutput{}, fmt.Errorf("read ollama response: %w", err)
		}

		if httpResp.StatusCode < 200 || httpResp.StatusCode >= 300 {
			return TranslateOutput{}, fmt.Errorf("ollama api returned status %d: %s", httpResp.StatusCode, string(respBytes))
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
			return TranslateOutput{}, fmt.Errorf("decode ollama response: %w", err)
		}
		if len(parsed.Choices) == 0 {
			return TranslateOutput{}, errors.New("ollama response has no choices")
		}

		candidate, err = extractJSONPayload(parsed.Choices[0].Message.Content)
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

	return TranslateOutput{
		Candidate: candidate,
		Model:     model,
		Meta:      meta,
	}, nil
}

func parseOllamaStream(reader io.Reader) (responseID string, responseModel string, usage map[string]any, candidate []byte, err error) {
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
			return "", "", nil, nil, fmt.Errorf("ollama stream returned error: %s", chunk.Error.Message)
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
		return "", "", nil, nil, fmt.Errorf("read ollama stream: %w", scanErr)
	}

	content := strings.TrimSpace(contentBuilder.String())
	if content == "" {
		return "", "", nil, nil, errors.New("ollama stream has no content")
	}
	candidate, err = extractJSONPayload(content)
	if err != nil {
		return "", "", nil, nil, err
	}
	return responseID, responseModel, usage, candidate, nil
}

func ollamaSystemPrompt() string {
	return strings.TrimSpace(`
You are HaruhiDB action protocol translator.
Convert user natural-language requests into exactly one JSON object that matches HaruhiDB Action Protocol request envelope.

Hard constraints:
1) Output MUST be valid JSON and must contain only these top-level keys:
"version", "request_id", "mode", "action", "args".
2) "version" must be "v1" or "v2".
3) "mode" must be "read_only" or "read_write".
4) "args" must always be a JSON object (never array or null).
5) Do not include markdown fences or explanation text.
6) Return only one JSON object.

Action set and args schema:
- list_tables: {}
- table_exists: {"table": string}
- describe_table: {"table": string}
- get_by_primary_int: {"table": string, "key": int}
- scan_all: {"table": string, "limit": int(optional)}
- scan_primary_int_range: {"table": string, "start_key": int, "end_key": int, "limit": int(optional)}
- insert_row: {"table": string, "values": object}
- update_by_primary_int: {"table": string, "key": int, "values": object}
- delete_by_primary_int: {"table": string, "key": int}
- create_table: {"table": string, "columns": [{"name": string, "type": "BOOLEAN|TINYINT|SMALLINT|INTEGER|BIGINT|FLOAT|DOUBLE|DECIMAL|VARCHAR", "nullable": bool, "length": uint(optional)}]}
- drop_table: {"table": string}
- create_primary_int_index: {"table": string, "index": string}
- drop_index: {"table": string, "index": string}
- batch: {"requests": [{"action": string, "args": object}], "stop_on_error": bool(optional)}

Version compatibility:
- v1 supports: list_tables, table_exists, describe_table, get_by_primary_int, scan_all, scan_primary_int_range, insert_row, update_by_primary_int, delete_by_primary_int, batch.
- v2 supports all v1 actions, plus: create_table, drop_table, create_primary_int_index, drop_index.

Mode compatibility:
- read_only must not contain any write action.
- write actions are: insert_row, update_by_primary_int, delete_by_primary_int, create_table, drop_table, create_primary_int_index, drop_index.
- For batch, sub-actions must follow the same mode/version rules.
`)
}

func buildOllamaUserPrompt(in TranslateInput) (string, error) {
	catalogJSON, err := json.Marshal(in.Catalog)
	if err != nil {
		return "", fmt.Errorf("marshal catalog snapshot: %w", err)
	}

	mode := in.Mode
	if mode == "" {
		mode = action.ModeReadOnly
	}

	parts := []string{
		"Translate the natural request into a strict HaruhiDB protocol envelope.",
		fmt.Sprintf("request_id: %s", in.RequestID),
		fmt.Sprintf("mode: %s", mode),
		fmt.Sprintf("allowed_actions_for_mode: %s", strings.Join(allowedActionsForMode(mode), ", ")),
		"version_rule: use v2 only when action requires v2; otherwise prefer v1.",
		"args_rule: all args must be JSON objects with exact fields only.",
		fmt.Sprintf("natural_request: %s", in.NaturalRequest),
		fmt.Sprintf("catalog_snapshot_json: %s", string(catalogJSON)),
	}

	if strings.TrimSpace(in.RepairHint) != "" {
		parts = append(parts, fmt.Sprintf("previous_output_error: %s", in.RepairHint))
		parts = append(parts, "Regenerate and strictly fix the error above.")
	}

	return strings.Join(parts, "\n"), nil
}

func allowedActionsForMode(mode action.Mode) []string {
	if mode == action.ModeReadOnly {
		return []string{
			string(action.ActionListTables),
			string(action.ActionTableExists),
			string(action.ActionDescribeTable),
			string(action.ActionGetByPrimaryInt),
			string(action.ActionScanAll),
			string(action.ActionScanPrimaryIntRange),
			string(action.ActionBatch),
		}
	}

	return []string{
		string(action.ActionListTables),
		string(action.ActionTableExists),
		string(action.ActionDescribeTable),
		string(action.ActionGetByPrimaryInt),
		string(action.ActionScanAll),
		string(action.ActionScanPrimaryIntRange),
		string(action.ActionInsertRow),
		string(action.ActionUpdateByPrimaryInt),
		string(action.ActionDeleteByPrimaryInt),
		string(action.ActionCreateTable),
		string(action.ActionDropTable),
		string(action.ActionCreatePrimaryIndex),
		string(action.ActionDropIndex),
		string(action.ActionBatch),
	}
}

func extractJSONPayload(content string) ([]byte, error) {
	trimmed := strings.TrimSpace(content)
	if trimmed == "" {
		return nil, errors.New("model content is empty")
	}

	candidates := collectJSONCandidates(trimmed)
	if len(candidates) == 0 {
		return nil, errors.New("model content does not contain a valid JSON object")
	}

	best, ok := selectBestJSONCandidate(candidates)
	if !ok {
		return nil, errors.New("model content does not contain a valid JSON object")
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

	version := ""
	if value, ok := payload["version"].(string); ok {
		version = strings.TrimSpace(value)
	}

	mode := action.Mode("")
	if value, ok := payload["mode"].(string); ok {
		mode = action.Mode(strings.TrimSpace(value))
	}

	actionName := ""
	parsedAction := action.Action("")
	if value, ok := payload["action"].(string); ok {
		actionName = strings.TrimSpace(value)
		parsedAction = action.Action(actionName)
	}

	for _, key := range requiredKeys {
		if _, ok := payload[key]; ok {
			matchedKeys++
			score += 2
		}
	}

	if version != "" {
		if action.SupportedVersion(version) {
			score += 4
		} else {
			score -= 2
		}
	}

	if value, ok := payload["request_id"].(string); ok && strings.TrimSpace(value) != "" {
		score += 2
	}

	if mode.Valid() {
		score += 4
	} else if mode != "" {
		score -= 2
	}

	if actionName != "" {
		score += 6
		if parsedAction.Valid() {
			score += 3
			if version != "" && action.ActionSupportedInVersion(version, parsedAction) {
				score += 4
			}
			if mode == action.ModeReadOnly && parsedAction.IsWrite() {
				score -= 6
			}
		} else {
			score -= 3
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
