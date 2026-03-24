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
	transporthttp "haruhidb-go/internal/transport/http"
)

const (
	defaultListenAddr = ":8080"
	defaultTimeout    = 30 * time.Second
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
	dbPath     string
	allowWrite bool
	timeout    time.Duration
	listenAddr string
}

func defaultServeOptions() serveOptions {
	return serveOptions{
		allowWrite: true,
		timeout:    defaultTimeout,
		listenAddr: defaultListenAddr,
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
	if err := fs.Parse(args); err != nil {
		return err
	}

	opts.dbPath = strings.TrimSpace(opts.dbPath)
	opts.listenAddr = strings.TrimSpace(opts.listenAddr)
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
	runtimeManager, err := app.NewRuntimeManager(app.RuntimeManagerConfig{
		DefaultDBPath:  opts.dbPath,
		AllowWrite:     opts.allowWrite,
		RequestTimeout: opts.timeout,
	})
	if err != nil {
		return nil, nil, err
	}

	return runtimeManager, func() {
		_ = runtimeManager.Close()
	}, nil
}

func printUsage(writer io.Writer) {
	_, _ = fmt.Fprintln(writer, "usage: haruhidb serve [options]")
	_, _ = fmt.Fprintln(writer, "subcommands:")
	_, _ = fmt.Fprintln(writer, "  serve   run HTTP+Web service (only supported subcommand)")
	_, _ = fmt.Fprintln(writer, "tip: use --config <file> (or HARUHIDB_CONFIG)")
}
