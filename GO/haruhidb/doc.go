// Package haruhidb is the primary integration shell over HaruhiDB's C ABI.
//
// Default build behavior:
//   - Headers are resolved from ../../CXX/src/include
//   - The shared library is linked from ../../CXX/build/src/capi
//
// To override these defaults for CI or custom layouts, set CGO_CFLAGS and
// CGO_LDFLAGS before running go build or go test.
//
// Build order must be:
//  1. Build CXX/src/capi/libharuhidb_capi.so
//  2. Run C API related ctest cases
//  3. Run go test ./...
//
// The repository provides scripts/api_gate.sh as a single gate entry that
// enforces this order and keeps Go/C ABI alignment deterministic.
//
// Error model:
//   - Go APIs keep existing method signatures
//   - Errors from C _ex APIs are converted into typed *Error values
//   - *Error carries Op + Code + Message and supports errors.Is/errors.As
//
// First version limitations:
//   - NULL writes are not supported yet
//   - The exposed index helper only supports the current
//     "first INTEGER NOT NULL column" index path
package haruhidb
