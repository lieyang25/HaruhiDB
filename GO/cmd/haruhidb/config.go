package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"strings"
	"time"
)

const (
	defaultConfigEnvKey = "HARUHIDB_CONFIG"
)

const (
	llmBackendNone             = "none"
	llmBackendOpenAI           = "openai"
	llmBackendOpenAICompatible = "openai_compatible"
	llmBackendOllama           = "ollama"
)

type runtimeConfig struct {
	Common commonConfig `json:"common"`
	LLM    llmConfig    `json:"llm"`
	Serve  serveConfig  `json:"serve"`
	Run    runConfig    `json:"run"`
	NL     nlConfig     `json:"nl"`
	Shell  shellConfig  `json:"shell"`
}

type commonConfig struct {
	DBPath     *string `json:"db_path"`
	AllowWrite *bool   `json:"allow_write"`
	Timeout    *string `json:"timeout"`
}

type llmConfig struct {
	Backend     *string `json:"backend"`
	Ollama      *bool   `json:"ollama"`
	Stream      *bool   `json:"stream"`
	APIKey      *string `json:"api_key"`
	BaseURL     *string `json:"base_url"`
	Model       *string `json:"model"`
	OllamaModel *string `json:"ollama_model"`
	Reasoning   *string `json:"reasoning_effort"`
	Examples    *string `json:"examples_path"`
}

type serveConfig struct {
	Listen             *string `json:"listen"`
	MaxBodyBytes       *int64  `json:"max_body_bytes"`
	AuthToken          *string `json:"auth_token"`
	RateLimitPerMinute *int    `json:"rate_limit_per_minute"`
	TrustProxyHeaders  *bool   `json:"trust_proxy_headers"`
}

type runConfig struct {
	Input  *string `json:"input"`
	JSON   *string `json:"json"`
	Pretty *bool   `json:"pretty"`
}

type nlConfig struct {
	Input     *string `json:"input"`
	InputFile *string `json:"input_file"`
	Mode      *string `json:"mode"`
	Execute   *bool   `json:"execute"`
	Pretty    *bool   `json:"pretty"`
}

type shellConfig struct {
	Mode *string `json:"mode"`
}

func resolveConfig(args []string) (runtimeConfig, string, error) {
	path, err := discoverConfigPath(args)
	if err != nil {
		return runtimeConfig{}, "", err
	}
	cfg, err := loadRuntimeConfig(path)
	if err != nil {
		return runtimeConfig{}, "", err
	}
	return cfg, path, nil
}

func discoverConfigPath(args []string) (string, error) {
	path := strings.TrimSpace(os.Getenv(defaultConfigEnvKey))
	for i := 0; i < len(args); i++ {
		arg := strings.TrimSpace(args[i])
		switch {
		case arg == "--config":
			if i+1 >= len(args) {
				return "", errors.New("missing value for --config")
			}
			path = strings.TrimSpace(args[i+1])
			i++
		case strings.HasPrefix(arg, "--config="):
			path = strings.TrimSpace(strings.TrimPrefix(arg, "--config="))
		}
	}
	return path, nil
}

func loadRuntimeConfig(path string) (runtimeConfig, error) {
	var cfg runtimeConfig
	path = strings.TrimSpace(path)
	if path == "" {
		return cfg, nil
	}

	raw, err := os.ReadFile(path)
	if err != nil {
		return runtimeConfig{}, fmt.Errorf("read config file %q: %w", path, err)
	}

	dec := json.NewDecoder(bytes.NewReader(raw))
	dec.UseNumber()
	if err := dec.Decode(&cfg); err != nil {
		return runtimeConfig{}, fmt.Errorf("decode config file %q: %w", path, err)
	}

	var trailing any
	if err := dec.Decode(&trailing); err != io.EOF {
		if err == nil {
			return runtimeConfig{}, fmt.Errorf("decode config file %q: unexpected trailing JSON input", path)
		}
		return runtimeConfig{}, fmt.Errorf("decode config file %q: %w", path, err)
	}
	return cfg, nil
}

func applyCommonConfig(opts *commonOptions, cfg runtimeConfig) error {
	if opts == nil {
		return nil
	}

	if cfg.Common.DBPath != nil {
		opts.dbPath = strings.TrimSpace(*cfg.Common.DBPath)
	}
	if cfg.Common.AllowWrite != nil {
		opts.allowWrite = *cfg.Common.AllowWrite
	}
	if cfg.Common.Timeout != nil {
		duration, err := time.ParseDuration(strings.TrimSpace(*cfg.Common.Timeout))
		if err != nil {
			return fmt.Errorf("invalid common.timeout: %w", err)
		}
		opts.timeout = duration
	}

	if cfg.LLM.Backend != nil {
		opts.llmBackend = strings.TrimSpace(*cfg.LLM.Backend)
	}
	if cfg.LLM.Ollama != nil {
		opts.ollama = *cfg.LLM.Ollama
	}
	if cfg.LLM.Stream != nil {
		opts.stream = *cfg.LLM.Stream
	}
	if cfg.LLM.APIKey != nil {
		opts.openAIAPIKey = strings.TrimSpace(*cfg.LLM.APIKey)
	}
	if cfg.LLM.BaseURL != nil {
		opts.openAIBaseURL = strings.TrimSpace(*cfg.LLM.BaseURL)
	}
	if cfg.LLM.Model != nil {
		opts.openAIModel = strings.TrimSpace(*cfg.LLM.Model)
	}
	if cfg.LLM.OllamaModel != nil {
		opts.ollamaModel = strings.TrimSpace(*cfg.LLM.OllamaModel)
	}
	if cfg.LLM.Reasoning != nil {
		opts.reasoningEffort = strings.TrimSpace(*cfg.LLM.Reasoning)
	}
	if cfg.LLM.Examples != nil {
		opts.examplesPath = strings.TrimSpace(*cfg.LLM.Examples)
	}

	return nil
}
