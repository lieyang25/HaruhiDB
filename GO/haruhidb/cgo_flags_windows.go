//go:build cgo && windows

package haruhidb

/*
#cgo CFLAGS: -I${SRCDIR}/../../CXX/src/include
#cgo LDFLAGS: -L${SRCDIR}/../../CXX/build/src/capi -L${SRCDIR}/../../CXX/build/src/capi/Debug -L${SRCDIR}/../../CXX/build/src/capi/Release -L${SRCDIR}/../../CXX/build/src/capi/RelWithDebInfo -L${SRCDIR}/../../CXX/build-mingw/src/capi -lharuhidb_capi
*/
import "C"
