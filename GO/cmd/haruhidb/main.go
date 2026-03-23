package main

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"strings"
	"time"

	"haruhidb-go/haruhidb"
	"haruhidb-go/internal/action"
	"haruhidb-go/internal/app"
	"haruhidb-go/internal/nl"
	transporthttp "haruhidb-go/internal/transport/http"
)

var translatorFactory = buildTranslator

const (
	defaultOllamaBaseURL = "http://127.0.0.1:11434"
	defaultOllamaModel   = "qwen2.5-coder:0.5b"
)

func main() {
	if err := run(os.Args[1:], os.Stdin, os.Stdout, os.Stderr); err != nil {
		_, _ = fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run(args []string, stdin io.Reader, stdout io.Writer, stderr io.Writer) error {
	if len(args) == 0 {
		printUsage(stderr)
		return errors.New("missing subcommand")
	}

	switch args[0] {
	case "serve":
		return runServe(args[1:], stdout, stderr)
	case "run":
		return runRun(args[1:], stdin, stdout, stderr)
	case "nl":
		return runNL(args[1:], stdin, stdout, stderr)
	case "shell":
		return runShell(args[1:], stdin, stdout, stderr)
	case "help", "-h", "--help":
		printUsage(stdout)
		return nil
	default:
		printUsage(stderr)
		return fmt.Errorf("unknown subcommand %q", args[0])
	}
}

type commonOptions struct {
	dbPath          string
	allowWrite      bool
	timeout         time.Duration
	llmBackend      string
	openAIAPIKey    string
	openAIBaseURL   string
	openAIModel     string
	reasoningEffort string
	examplesPath    string
	ollama          bool
	ollamaModel     string
}

func runServe(args []string, stdout io.Writer, stderr io.Writer) error {
	cfg, configPath, err := resolveConfig(args)
	if err != nil {
		return err
	}

	var opts commonOptions
	if err := applyCommonConfig(&opts, cfg); err != nil {
		return err
	}
	var listenAddr string
	var maxBodyBytes int64
	var authToken string
	var rateLimitPerMinute int
	var trustProxyHeaders bool

	listenAddr = ":8080"
	maxBodyBytes = 1 << 20
	if cfg.Serve.Listen != nil {
		listenAddr = strings.TrimSpace(*cfg.Serve.Listen)
	}
	if cfg.Serve.MaxBodyBytes != nil {
		maxBodyBytes = *cfg.Serve.MaxBodyBytes
	}
	if cfg.Serve.AuthToken != nil {
		authToken = strings.TrimSpace(*cfg.Serve.AuthToken)
	}
	if cfg.Serve.RateLimitPerMinute != nil {
		rateLimitPerMinute = *cfg.Serve.RateLimitPerMinute
	}
	if cfg.Serve.TrustProxyHeaders != nil {
		trustProxyHeaders = *cfg.Serve.TrustProxyHeaders
	}

	fs := flag.NewFlagSet("serve", flag.ContinueOnError)
	fs.SetOutput(stderr)
	bindCommonFlags(fs, &opts)
	bindLLMFlags(fs, &opts)
	fs.StringVar(&configPath, "config", configPath, "path to JSON config file (or HARUHIDB_CONFIG env)")
	fs.StringVar(&listenAddr, "listen", listenAddr, "listen address")
	fs.Int64Var(&maxBodyBytes, "max-body-bytes", maxBodyBytes, "max HTTP request body bytes")
	fs.StringVar(&authToken, "auth-token", authToken, "optional bearer token for HTTP API")
	fs.IntVar(&rateLimitPerMinute, "rate-limit-per-minute", rateLimitPerMinute, "optional per-client request limit per minute")
	fs.BoolVar(&trustProxyHeaders, "trust-proxy-headers", trustProxyHeaders, "trust X-Forwarded-For/X-Real-IP for rate-limit client IP")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if err := normalizeLLMOptions(&opts); err != nil {
		return err
	}

	service, cleanup, err := buildService(opts, false)
	if err != nil {
		return err
	}
	defer cleanup()

	handler := transporthttp.NewHandler(service, transporthttp.Config{
		MaxBodyBytes:       maxBodyBytes,
		AuthToken:          authToken,
		RateLimitPerMinute: rateLimitPerMinute,
		TrustProxyHeaders:  trustProxyHeaders,
		Logger:             log.New(stderr, "http ", log.LstdFlags),
	})

	_, _ = fmt.Fprintf(stdout, "serving on %s\n", listenAddr)
	return http.ListenAndServe(listenAddr, handler)
}

func runRun(args []string, stdin io.Reader, stdout io.Writer, stderr io.Writer) error {
	cfg, configPath, err := resolveConfig(args)
	if err != nil {
		return err
	}

	var opts commonOptions
	if err := applyCommonConfig(&opts, cfg); err != nil {
		return err
	}
	var inputPath string
	var jsonInput string
	var pretty bool

	pretty = true
	if cfg.Run.Pretty != nil {
		pretty = *cfg.Run.Pretty
	}

	fs := flag.NewFlagSet("run", flag.ContinueOnError)
	fs.SetOutput(stderr)
	bindCommonFlags(fs, &opts)
	fs.StringVar(&configPath, "config", configPath, "path to JSON config file (or HARUHIDB_CONFIG env)")
	fs.StringVar(&inputPath, "input", "", "input JSON file path, use - for stdin")
	fs.StringVar(&jsonInput, "json", "", "inline JSON request payload")
	fs.BoolVar(&pretty, "pretty", pretty, "pretty print output")
	if err := fs.Parse(args); err != nil {
		return err
	}

	payload, err := readJSONPayload(inputPath, jsonInput, stdin)
	if err != nil {
		return err
	}

	service, cleanup, err := buildService(opts, false)
	if err != nil {
		return err
	}
	defer cleanup()

	resp, err := service.ExecuteJSON(context.Background(), payload)
	if err != nil {
		return err
	}
	return writeJSONOutput(stdout, resp, pretty)
}

func runNL(args []string, stdin io.Reader, stdout io.Writer, stderr io.Writer) error {
	cfg, configPath, err := resolveConfig(args)
	if err != nil {
		return err
	}

	var opts commonOptions
	if err := applyCommonConfig(&opts, cfg); err != nil {
		return err
	}
	var inputText string
	var inputPath string
	var mode string
	var execute bool
	var pretty bool

	mode = string(action.ModeReadOnly)
	pretty = true
	if cfg.NL.Mode != nil {
		mode = strings.TrimSpace(*cfg.NL.Mode)
	}
	if cfg.NL.Execute != nil {
		execute = *cfg.NL.Execute
	}
	if cfg.NL.Pretty != nil {
		pretty = *cfg.NL.Pretty
	}

	fs := flag.NewFlagSet("nl", flag.ContinueOnError)
	fs.SetOutput(stderr)
	bindCommonFlags(fs, &opts)
	bindLLMFlags(fs, &opts)
	fs.StringVar(&configPath, "config", configPath, "path to JSON config file (or HARUHIDB_CONFIG env)")
	fs.StringVar(&inputText, "input", "", "natural language input text")
	fs.StringVar(&inputPath, "input-file", "", "natural language input file path, use - for stdin")
	fs.StringVar(&mode, "mode", mode, "request mode: read_only or read_write")
	fs.BoolVar(&execute, "execute", execute, "execute candidate JSON when translation is valid")
	fs.BoolVar(&pretty, "pretty", pretty, "pretty print output")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if err := normalizeLLMOptions(&opts); err != nil {
		return err
	}

	naturalInput, err := readTextInput(inputPath, inputText, stdin)
	if err != nil {
		return err
	}

	service, cleanup, err := buildService(opts, true)
	if err != nil {
		return err
	}
	defer cleanup()

	translateResult, err := service.TranslateNL(context.Background(), app.NLRequest{
		RequestID: fmt.Sprintf("nl-cli-%d", time.Now().UnixNano()),
		Input:     naturalInput,
		Mode:      action.Mode(mode),
	})
	if err != nil {
		return err
	}

	translatedBytes, err := json.Marshal(translateResult)
	if err != nil {
		return err
	}
	if err := writeJSONOutput(stdout, translatedBytes, pretty); err != nil {
		return err
	}

	if !execute {
		return nil
	}
	if !translateResult.Valid || len(translateResult.CandidateRaw) == 0 {
		return errors.New("translation result is not executable")
	}

	executeResp, err := service.ExecuteJSON(context.Background(), translateResult.CandidateRaw)
	if err != nil {
		return err
	}
	_, _ = fmt.Fprintln(stdout)
	return writeJSONOutput(stdout, executeResp, pretty)
}

func runShell(args []string, stdin io.Reader, stdout io.Writer, stderr io.Writer) error {
	cfg, configPath, err := resolveConfig(args)
	if err != nil {
		return err
	}

	var opts commonOptions
	if err := applyCommonConfig(&opts, cfg); err != nil {
		return err
	}
	var mode string
	mode = string(action.ModeReadOnly)
	if cfg.Shell.Mode != nil {
		mode = strings.TrimSpace(*cfg.Shell.Mode)
	}

	fs := flag.NewFlagSet("shell", flag.ContinueOnError)
	fs.SetOutput(stderr)
	bindCommonFlags(fs, &opts)
	bindLLMFlags(fs, &opts)
	fs.StringVar(&configPath, "config", configPath, "path to JSON config file (or HARUHIDB_CONFIG env)")
	fs.StringVar(&mode, "mode", mode, "default mode for :nl translation")
	if err := fs.Parse(args); err != nil {
		return err
	}
	if err := normalizeLLMOptions(&opts); err != nil {
		return err
	}

	service, cleanup, err := buildService(opts, false)
	if err != nil {
		return err
	}
	defer cleanup()

	scanner := bufio.NewScanner(stdin)
	scanner.Buffer(make([]byte, 0, 4096), 1<<20)

	_, _ = fmt.Fprintln(stdout, "HaruhiDB shell")
	_, _ = fmt.Fprintln(stdout, "commands: :json <payload>, :jsonfile <path>, :nl <text>, :status, :help, :quit")
	printShellStatus(stdout, service, mode)

	for {
		_, _ = fmt.Fprint(stdout, "haruhidb> ")
		if !scanner.Scan() {
			if err := scanner.Err(); err != nil {
				return err
			}
			return nil
		}
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}

		switch {
		case line == ":quit" || line == ":exit":
			return nil
		case line == ":status":
			printShellStatus(stdout, service, mode)
		case line == ":help":
			_, _ = fmt.Fprintln(stdout, "commands: :json <payload>, :jsonfile <path>, :nl <text>, :status, :help, :quit")
		case strings.HasPrefix(line, ":json "):
			payload := strings.TrimSpace(strings.TrimPrefix(line, ":json "))
			if err := executeAndPrint(service, []byte(payload), stdout); err != nil {
				_, _ = fmt.Fprintln(stdout, "error:", err)
			}
		case strings.HasPrefix(line, ":jsonfile "):
			path := strings.TrimSpace(strings.TrimPrefix(line, ":jsonfile "))
			raw, err := os.ReadFile(path)
			if err != nil {
				_, _ = fmt.Fprintln(stdout, "error:", err)
				continue
			}
			if err := executeAndPrint(service, raw, stdout); err != nil {
				_, _ = fmt.Fprintln(stdout, "error:", err)
			}
		case strings.HasPrefix(line, ":nl "):
			text := strings.TrimSpace(strings.TrimPrefix(line, ":nl "))
			result, err := service.TranslateNL(context.Background(), app.NLRequest{
				RequestID: fmt.Sprintf("shell-%d", time.Now().UnixNano()),
				Input:     text,
				Mode:      action.Mode(mode),
			})
			if err != nil {
				_, _ = fmt.Fprintln(stdout, "error:", err)
				continue
			}
			resultBytes, err := json.Marshal(result)
			if err != nil {
				_, _ = fmt.Fprintln(stdout, "error:", err)
				continue
			}
			if err := writeJSONOutput(stdout, resultBytes, true); err != nil {
				_, _ = fmt.Fprintln(stdout, "error:", err)
				continue
			}
			if !result.Valid || len(result.CandidateRaw) == 0 {
				continue
			}

			_, _ = fmt.Fprint(stdout, "execute candidate? [y/N]: ")
			if !scanner.Scan() {
				if err := scanner.Err(); err != nil {
					return err
				}
				return nil
			}
			answer := strings.ToLower(strings.TrimSpace(scanner.Text()))
			if answer != "y" && answer != "yes" {
				continue
			}
			if err := executeAndPrint(service, result.CandidateRaw, stdout); err != nil {
				_, _ = fmt.Fprintln(stdout, "error:", err)
			}
		default:
			if strings.HasPrefix(line, "{") {
				if err := executeAndPrint(service, []byte(line), stdout); err != nil {
					_, _ = fmt.Fprintln(stdout, "error:", err)
				}
				continue
			}
			_, _ = fmt.Fprintln(stdout, "unknown command, use :help")
		}
	}
}

func bindCommonFlags(fs *flag.FlagSet, opts *commonOptions) {
	timeoutDefault := opts.timeout
	if timeoutDefault <= 0 {
		timeoutDefault = 15 * time.Second
	}
	fs.StringVar(&opts.dbPath, "db-path", opts.dbPath, "database file path")
	fs.BoolVar(&opts.allowWrite, "allow-write", opts.allowWrite, "allow write actions")
	fs.DurationVar(&opts.timeout, "timeout", timeoutDefault, "request timeout")
}

func bindLLMFlags(fs *flag.FlagSet, opts *commonOptions) {
	fs.StringVar(&opts.llmBackend, "llm-backend", opts.llmBackend, "llm backend: none, openai, openai_compatible, ollama")
	fs.StringVar(&opts.openAIAPIKey, "openai-api-key", opts.openAIAPIKey, "openai api key (or OPENAI_API_KEY env)")
	fs.StringVar(&opts.openAIAPIKey, "api-key", opts.openAIAPIKey, "alias of --openai-api-key")
	fs.StringVar(&opts.openAIBaseURL, "openai-base-url", opts.openAIBaseURL, "openai base url")
	fs.StringVar(&opts.openAIBaseURL, "base-url", opts.openAIBaseURL, "alias of --openai-base-url")
	fs.StringVar(&opts.openAIModel, "openai-model", opts.openAIModel, "openai model name")
	fs.StringVar(&opts.openAIModel, "model", opts.openAIModel, "alias of --openai-model")
	fs.StringVar(&opts.reasoningEffort, "reasoning-effort", opts.reasoningEffort, "reasoning effort for capable models: off, low, medium, high")
	fs.StringVar(&opts.examplesPath, "examples-path", opts.examplesPath, "path to action examples document injected into NL translation prompt")
	fs.BoolVar(&opts.ollama, "ollama", opts.ollama, "use local Ollama endpoint (http://127.0.0.1:11434)")
	fs.StringVar(&opts.ollamaModel, "ollama-model", opts.ollamaModel, "model name used with --ollama (default qwen2.5-coder:0.5b)")
}

func normalizeLLMOptions(opts *commonOptions) error {
	if opts == nil {
		return nil
	}
	backend := strings.ToLower(strings.TrimSpace(opts.llmBackend))
	opts.llmBackend = backend

	switch backend {
	case "":
	case llmBackendNone:
		hasLLMOverrides := opts.ollama ||
			strings.TrimSpace(opts.ollamaModel) != "" ||
			strings.TrimSpace(opts.openAIAPIKey) != "" ||
			strings.TrimSpace(opts.openAIBaseURL) != "" ||
			strings.TrimSpace(opts.openAIModel) != ""
		if hasLLMOverrides {
			backend = ""
			opts.llmBackend = backend
			if opts.ollama || strings.TrimSpace(opts.ollamaModel) != "" {
				backend = llmBackendOllama
				opts.llmBackend = backend
			}
			break
		}
		opts.openAIAPIKey = ""
		opts.openAIBaseURL = ""
		opts.openAIModel = ""
		opts.reasoningEffort = ""
		opts.examplesPath = ""
		opts.ollama = false
		opts.ollamaModel = ""
		return nil
	case llmBackendOpenAI, llmBackendOpenAICompatible:
	case llmBackendOllama:
		opts.ollama = true
	default:
		return fmt.Errorf("invalid llm backend %q: expected one of none/openai/openai_compatible/ollama", opts.llmBackend)
	}

	if !opts.ollama && strings.TrimSpace(opts.ollamaModel) == "" {
		return nil
	}
	if strings.TrimSpace(opts.openAIBaseURL) == "" {
		opts.openAIBaseURL = defaultOllamaBaseURL
	}
	if strings.TrimSpace(opts.openAIModel) == "" {
		model := strings.TrimSpace(opts.ollamaModel)
		if model == "" {
			model = defaultOllamaModel
		}
		opts.openAIModel = model
	}
	return nil
}

func printShellStatus(stdout io.Writer, service *app.ActionService, mode string) {
	nlEnabled := service != nil && service.HasTranslator()
	if nlEnabled {
		_, _ = fmt.Fprintf(stdout, "status: mode=%s, nl=enabled\n", mode)
		return
	}
	_, _ = fmt.Fprintf(stdout, "status: mode=%s, nl=disabled\n", mode)
	_, _ = fmt.Fprintln(stdout, "tip: add --ollama (or --openai-api-key) to enable :nl")
}

func buildService(opts commonOptions, requireTranslator bool) (*app.ActionService, func(), error) {
	if strings.TrimSpace(opts.dbPath) == "" {
		return nil, nil, errors.New("--db-path is required")
	}

	db, err := haruhidb.Open(opts.dbPath, haruhidb.OpenOptions{})
	if err != nil {
		return nil, nil, err
	}

	translator, err := translatorFactory(opts)
	if err != nil {
		_ = db.Close()
		return nil, nil, err
	}
	if requireTranslator && translator == nil {
		_ = db.Close()
		return nil, nil, errors.New("translator is required; provide OPENAI_API_KEY, --openai-api-key, or use --ollama")
	}

	service, err := app.NewActionService(app.Config{
		DB:             db,
		AllowWrite:     opts.allowWrite,
		RequestTimeout: opts.timeout,
		Translator:     translator,
	})
	if err != nil {
		_ = db.Close()
		return nil, nil, err
	}

	return service, func() {
		_ = db.Close()
	}, nil
}

func buildTranslator(opts commonOptions) (nl.Translator, error) {
	if strings.EqualFold(strings.TrimSpace(opts.llmBackend), llmBackendNone) {
		return nil, nil
	}

	apiKey := strings.TrimSpace(opts.openAIAPIKey)
	if apiKey == "" {
		apiKey = strings.TrimSpace(os.Getenv("OPENAI_API_KEY"))
	}
	baseURL := strings.TrimSpace(opts.openAIBaseURL)
	model := strings.TrimSpace(opts.openAIModel)

	if apiKey == "" && baseURL == "" && model == "" {
		return nil, nil
	}

	promptExamplesPath := strings.TrimSpace(opts.examplesPath)
	promptExamples := ""
	if promptExamplesPath != "" {
		rawExamples, err := os.ReadFile(promptExamplesPath)
		if err != nil {
			return nil, fmt.Errorf("read llm examples file %q: %w", promptExamplesPath, err)
		}
		promptExamples = strings.TrimSpace(string(rawExamples))
	}

	translator, err := nl.NewOpenAITranslator(nl.OpenAIConfig{
		APIKey:          apiKey,
		BaseURL:         baseURL,
		Model:           model,
		ReasoningEffort: strings.TrimSpace(opts.reasoningEffort),
		PromptExamples:  promptExamples,
		HTTPClient: &http.Client{
			Timeout: opts.timeout,
		},
	})
	if err != nil {
		return nil, err
	}
	return translator, nil
}

func readJSONPayload(inputPath string, jsonInput string, stdin io.Reader) ([]byte, error) {
	if strings.TrimSpace(jsonInput) != "" {
		if strings.TrimSpace(inputPath) != "" {
			return nil, errors.New("--json and --input cannot be used together")
		}
		return []byte(jsonInput), nil
	}

	switch strings.TrimSpace(inputPath) {
	case "":
		return io.ReadAll(stdin)
	case "-":
		return io.ReadAll(stdin)
	default:
		return os.ReadFile(inputPath)
	}
}

func readTextInput(inputPath string, text string, stdin io.Reader) (string, error) {
	text = strings.TrimSpace(text)
	if text != "" {
		if strings.TrimSpace(inputPath) != "" {
			return "", errors.New("--input and --input-file cannot be used together")
		}
		return text, nil
	}

	switch strings.TrimSpace(inputPath) {
	case "":
		raw, err := io.ReadAll(stdin)
		if err != nil {
			return "", err
		}
		return strings.TrimSpace(string(raw)), nil
	case "-":
		raw, err := io.ReadAll(stdin)
		if err != nil {
			return "", err
		}
		return strings.TrimSpace(string(raw)), nil
	default:
		raw, err := os.ReadFile(inputPath)
		if err != nil {
			return "", err
		}
		return strings.TrimSpace(string(raw)), nil
	}
}

func executeAndPrint(service *app.ActionService, raw []byte, stdout io.Writer) error {
	resp, err := service.ExecuteJSON(context.Background(), raw)
	if err != nil {
		return err
	}
	return writeJSONOutput(stdout, resp, true)
}

func writeJSONOutput(writer io.Writer, raw []byte, pretty bool) error {
	if pretty {
		prettyJSON, err := formatJSON(raw)
		if err == nil {
			raw = prettyJSON
		}
	}
	if _, err := writer.Write(raw); err != nil {
		return err
	}
	_, err := writer.Write([]byte("\n"))
	return err
}

func formatJSON(raw []byte) ([]byte, error) {
	var out bytes.Buffer
	if err := json.Indent(&out, raw, "", "  "); err != nil {
		return nil, err
	}
	return out.Bytes(), nil
}

func printUsage(writer io.Writer) {
	_, _ = fmt.Fprintln(writer, "usage: haruhidb <subcommand> [options]")
	_, _ = fmt.Fprintln(writer, "subcommands:")
	_, _ = fmt.Fprintln(writer, "  serve   run HTTP server")
	_, _ = fmt.Fprintln(writer, "  run     execute action JSON")
	_, _ = fmt.Fprintln(writer, "  nl      translate natural language to action JSON")
	_, _ = fmt.Fprintln(writer, "  shell   interactive shell")
	_, _ = fmt.Fprintln(writer, "tip: all subcommands support --config <file> (or HARUHIDB_CONFIG)")
}
