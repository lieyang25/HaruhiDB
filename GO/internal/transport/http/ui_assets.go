package httptransport

import (
	"embed"
	"io/fs"
	"net/http"
)

//go:embed ui/*
var uiAssets embed.FS

func buildUIFileServer() (http.Handler, error) {
	sub, err := fs.Sub(uiAssets, "ui")
	if err != nil {
		return nil, err
	}
	return http.FileServer(http.FS(sub)), nil
}
