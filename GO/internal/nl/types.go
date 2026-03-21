package nl

import (
	"context"

	"haruhidb-go/internal/action"
)

type ColumnInfo struct {
	Name     string `json:"name"`
	Type     string `json:"type"`
	Length   uint32 `json:"length,omitempty"`
	Nullable bool   `json:"nullable"`
}

type TableInfo struct {
	Name    string       `json:"name"`
	Columns []ColumnInfo `json:"columns"`
	Indexes []string     `json:"indexes"`
}

type CatalogSnapshot struct {
	Tables []TableInfo `json:"tables"`
}

type TranslateInput struct {
	RequestID      string
	NaturalRequest string
	Mode           action.Mode
	Catalog        CatalogSnapshot
	RepairHint     string
}

type TranslateOutput struct {
	Candidate []byte
	Model     string
	Meta      map[string]any
}

type Translator interface {
	Translate(ctx context.Context, in TranslateInput) (TranslateOutput, error)
}
