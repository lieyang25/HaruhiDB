package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"haruhidb-go/internal/app"
	"haruhidb-go/internal/nl"
	transporthttp "haruhidb-go/internal/transport/http"
)

var translatorFactory = buildTranslator

const (
	defaultOllamaBaseURL = "http://127.0.0.1:11434"
	defaultOllamaModel   = "qwen2.5-coder:3b"
	defaultListenAddr    = ":8080"
	defaultTimeout       = 120 * time.Second
)

func main() {
	if err := run(os.Args[1:], os.Stdout, os.Stderr); err != nil {
		_, _ = fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(args []string, stdout io.Writer, stderr io.Writer) error {
	if len(args) == 0 {
		printUsage(stderr)
		return errors.New("missing subcommand")
	}

	switch args[0] {
	case "serve":
		return runServe(args[1:], stdout, stderr)
	case "help", "-h", "--help":
		printUsage(stdout)
		return nil
	case "run", "nl", "shell":
		printUsage(stderr)
		return fmt.Errorf("subcommand %q has been removed; only \"serve\" is supported", args[0])
	default:
		printUsage(stderr)
		return fmt.Errorf("unknown subcommand %q", args[0])
	}
}

type serveOptions struct {
	dbPath         string
	allowWrite     bool
	timeout        time.Duration
	listenAddr     string
	ollamaBaseURL  string
	ollamaModel    string
	streamResponse bool
}

func defaultServeOptions() serveOptions {
	return serveOptions{
		allowWrite:     true,
		timeout:        defaultTimeout,
		listenAddr:     defaultListenAddr,
		ollamaBaseURL:  defaultOllamaBaseURL,
		ollamaModel:    defaultOllamaModel,
		streamResponse: false,
	}
}

func runServe(args []string, stdout io.Writer, stderr io.Writer) error {
	cfg, configPath, err := resolveConfig(args)
	if err != nil {
		return err
	}

	opts := defaultServeOptions()
	if err := applyServeConfig(&opts, cfg); err != nil {
		return err
	}

	fs := flag.NewFlagSet("serve", flag.ContinueOnError)
	fs.SetOutput(stderr)
	fs.StringVar(&configPath, "config", configPath, "path to JSON config file (or HARUHIDB_CONFIG env)")
	fs.StringVar(&opts.dbPath, "db-path", opts.dbPath, "database file path")
	fs.BoolVar(&opts.allowWrite, "allow-write", opts.allowWrite, "allow write actions")
	fs.DurationVar(&opts.timeout, "timeout", opts.timeout, "request timeout")
	fs.StringVar(&opts.listenAddr, "listen", opts.listenAddr, "listen address")
	fs.StringVar(&opts.ollamaModel, "model", opts.ollamaModel, "ollama model name")
	fs.StringVar(&opts.ollamaBaseURL, "base-url", opts.ollamaBaseURL, "ollama base URL")
	fs.BoolVar(&opts.streamResponse, "stream", opts.streamResponse, "enable streaming responses from Ollama")
	if err := fs.Parse(args); err != nil {
		return err
	}

	opts.dbPath = strings.TrimSpace(opts.dbPath)
	opts.listenAddr = strings.TrimSpace(opts.listenAddr)
	opts.ollamaModel = strings.TrimSpace(opts.ollamaModel)
	opts.ollamaBaseURL = strings.TrimSpace(opts.ollamaBaseURL)
	if opts.dbPath == "" {
		return errors.New("--db-path is required")
	}
	absDBPath, err := filepath.Abs(opts.dbPath)
	if err == nil {
		opts.dbPath = absDBPath
	}
	if opts.listenAddr == "" {
		opts.listenAddr = defaultListenAddr
	}
	if opts.timeout <= 0 {
		opts.timeout = defaultTimeout
	}
	if opts.ollamaModel == "" {
		opts.ollamaModel = defaultOllamaModel
	}
	if opts.ollamaBaseURL == "" {
		opts.ollamaBaseURL = defaultOllamaBaseURL
	}

	runtimeManager, cleanup, err := buildRuntimeManager(opts)
	if err != nil {
		return err
	}
	defer cleanup()

	handler := transporthttp.NewHandler(runtimeManager, transporthttp.Config{
		Logger: log.New(stderr, "http ", log.LstdFlags),
	})

	_, _ = fmt.Fprintf(stdout, "serving on %s\n", opts.listenAddr)
	_, _ = fmt.Fprintf(stdout, "db file: %s\n", opts.dbPath)
	return http.ListenAndServe(opts.listenAddr, handler)
}

func buildRuntimeManager(opts serveOptions) (*app.RuntimeManager, func(), error) {
	translator, err := translatorFactory(opts)
	if err != nil {
		return nil, nil, err
	}
	if translator == nil {
		return nil, nil, errors.New("ollama translator is required")
	}

	runtimeManager, err := app.NewRuntimeManager(app.RuntimeManagerConfig{
		DefaultDBPath:  opts.dbPath,
		AllowWrite:     opts.allowWrite,
		RequestTimeout: opts.timeout,
		Translator:     translator,
	})
	if err != nil {
		return nil, nil, err
	}

	return runtimeManager, func() {
		_ = runtimeManager.Close()
	}, nil
}

func buildTranslator(opts serveOptions) (nl.Translator, error) {
	translator, err := nl.NewOllamaTranslator(nl.OllamaConfig{
		BaseURL: opts.ollamaBaseURL,
		Model:   opts.ollamaModel,
		Stream:  opts.streamResponse,
		HTTPClient: &http.Client{
			Timeout: opts.timeout,
		},
	})
	if err != nil {
		return nil, err
	}
	return translator, nil
}

func printUsage(writer io.Writer) {
	_, _ = fmt.Fprintln(writer, "usage: haruhidb serve [options]")
	_, _ = fmt.Fprintln(writer, "subcommands:")
	_, _ = fmt.Fprintln(writer, "  serve   run HTTP+Web service (only supported subcommand)")
	_, _ = fmt.Fprintln(writer, "tip: use --config <file> (or HARUHIDB_CONFIG)")
}
