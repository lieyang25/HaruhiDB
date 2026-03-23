//go:build cgo && (linux || darwin)

package haruhidb

/*
#cgo CFLAGS: -I${SRCDIR}/../../CXX/src/include
#cgo LDFLAGS: -L${SRCDIR}/../../CXX/build/src/capi -lharuhidb_capi -Wl,-rpath,${SRCDIR}/../../CXX/build/src/capi
*/
import "C"
