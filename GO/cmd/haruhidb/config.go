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

type runtimeConfig struct {
	Common commonConfig `json:"common"`
	Serve  serveConfig  `json:"serve"`
}

type commonConfig struct {
	DBPath     *string `json:"db_path"`
	AllowWrite *bool   `json:"allow_write"`
	Timeout    *string `json:"timeout"`
}

type serveConfig struct {
	Listen *string `json:"listen"`
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

func applyServeConfig(opts *serveOptions, cfg runtimeConfig) error {
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

	if cfg.Serve.Listen != nil {
		opts.listenAddr = strings.TrimSpace(*cfg.Serve.Listen)
	}

	return nil
}
