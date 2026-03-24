package app

import (
	"errors"
	"fmt"
	"log"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"haruhidb-go/haruhidb"
	"haruhidb-go/internal/nl"
)

type RuntimeManagerConfig struct {
	DefaultDBPath  string
	AllowWrite     bool
	RequestTimeout time.Duration
	Translator     nl.Translator
	Logger         *log.Logger
}

type RuntimeManager struct {
	mu sync.RWMutex

	defaultDBPath  string
	allowWrite     bool
	requestTimeout time.Duration
	translator     nl.Translator
	logger         *log.Logger

	dbs      map[string]*haruhidb.DB
	services map[string]*ActionService
}

func NewRuntimeManager(cfg RuntimeManagerConfig) (*RuntimeManager, error) {
	defaultDBPath, err := canonicalDBPath(cfg.DefaultDBPath, "")
	if err != nil {
		return nil, fmt.Errorf("normalize default db path: %w", err)
	}
	if defaultDBPath == "" {
		return nil, errors.New("default db path must not be empty")
	}

	m := &RuntimeManager{
		defaultDBPath:  defaultDBPath,
		allowWrite:     cfg.AllowWrite,
		requestTimeout: cfg.RequestTimeout,
		translator:     cfg.Translator,
		logger:         cfg.Logger,
		dbs:            make(map[string]*haruhidb.DB),
		services:       make(map[string]*ActionService),
	}

	if _, _, err := m.Resolve(""); err != nil {
		return nil, err
	}
	return m, nil
}

func (m *RuntimeManager) DefaultDBPath() string {
	if m == nil {
		return ""
	}
	return m.defaultDBPath
}

func (m *RuntimeManager) Resolve(dbPath string) (*ActionService, string, error) {
	if m == nil {
		return nil, "", errors.New("runtime manager is nil")
	}

	resolvedPath, err := canonicalDBPath(dbPath, m.defaultDBPath)
	if err != nil {
		return nil, "", err
	}

	m.mu.RLock()
	service := m.services[resolvedPath]
	m.mu.RUnlock()
	if service != nil {
		return service, resolvedPath, nil
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	if existing := m.services[resolvedPath]; existing != nil {
		return existing, resolvedPath, nil
	}

	db, err := haruhidb.Open(resolvedPath, haruhidb.OpenOptions{})
	if err != nil {
		return nil, "", err
	}

	service, err = NewActionService(Config{
		DB:             db,
		AllowWrite:     m.allowWrite,
		RequestTimeout: m.requestTimeout,
		Translator:     m.translator,
		Logger:         m.logger,
	})
	if err != nil {
		_ = db.Close()
		return nil, "", err
	}

	m.dbs[resolvedPath] = db
	m.services[resolvedPath] = service
	return service, resolvedPath, nil
}

func (m *RuntimeManager) Close() error {
	if m == nil {
		return nil
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	var firstErr error
	for path, db := range m.dbs {
		if db == nil {
			continue
		}
		if err := db.Close(); err != nil && firstErr == nil {
			firstErr = fmt.Errorf("close db %q: %w", path, err)
		}
	}

	m.dbs = make(map[string]*haruhidb.DB)
	m.services = make(map[string]*ActionService)
	return firstErr
}

func canonicalDBPath(dbPath string, fallback string) (string, error) {
	candidate := strings.TrimSpace(dbPath)
	if candidate == "" {
		candidate = strings.TrimSpace(fallback)
	}
	if candidate == "" {
		return "", nil
	}

	cleaned := filepath.Clean(candidate)
	abs, err := filepath.Abs(cleaned)
	if err != nil {
		return "", err
	}
	return abs, nil
}
