package action

import (
	"encoding/json"
	"fmt"
	"testing"
)

func TestRegistryValidateEnvelopeConsistency(t *testing.T) {
	catalog := docCatalog()
	specs := PublicActionSpecs()
	if len(specs) == 0 {
		t.Fatal("expected non-empty public action registry")
	}

	for _, spec := range specs {
		if len(spec.Examples) == 0 {
			t.Fatalf("action %q has no example in registry", spec.Name)
		}

		example := deepCopyMap(spec.Examples[0])
		example["version"] = DefaultVersion
		example["request_id"] = fmt.Sprintf("req-registry-%s", spec.Name)
		example["mode"] = spec.Modes[0]
		example["action"] = spec.Name

		args, ok := example["args"].(map[string]any)
		if !ok {
			t.Fatalf("action %q example args is not object: %#v", spec.Name, example["args"])
		}
		if spec.Name == ActionCreateTable {
			args["table"] = "registry_new_table"
		}
		if spec.Name == ActionCreatePrimaryIndex {
			args["index"] = "idx_users_new"
		}

		raw, err := json.Marshal(example)
		if err != nil {
			t.Fatalf("marshal action %q example failed: %v", spec.Name, err)
		}

		if _, err := DecodeAndValidate(raw, catalog); err != nil {
			t.Fatalf("registry example for action %q failed validation: %v\npayload=%s", spec.Name, err, string(raw))
		}
	}
}

func TestRegistryModeVisibility(t *testing.T) {
	readOnly := make(map[string]struct{})
	for _, name := range PublicActionNamesForMode(ModeReadOnly) {
		readOnly[name] = struct{}{}
	}
	readWrite := make(map[string]struct{})
	for _, name := range PublicActionNamesForMode(ModeReadWrite) {
		readWrite[name] = struct{}{}
	}

	if _, ok := readOnly[string(ActionListTables)]; !ok {
		t.Fatalf("read_only actions must include %q", ActionListTables)
	}
	if _, ok := readOnly[string(ActionCreateTable)]; ok {
		t.Fatalf("read_only actions must not include %q", ActionCreateTable)
	}
	if _, ok := readWrite[string(ActionCreateTable)]; !ok {
		t.Fatalf("read_write actions must include %q", ActionCreateTable)
	}
	if _, ok := readWrite[string(ActionDropTable)]; !ok {
		t.Fatalf("read_write actions must include %q", ActionDropTable)
	}

	for _, spec := range PublicActionSpecs() {
		for _, mode := range []Mode{ModeReadOnly, ModeReadWrite} {
			supported := ActionSupportsMode(spec.Name, mode)
			seenInList := false
			switch mode {
			case ModeReadOnly:
				_, seenInList = readOnly[string(spec.Name)]
			case ModeReadWrite:
				_, seenInList = readWrite[string(spec.Name)]
			}
			if supported != seenInList {
				t.Fatalf("mode visibility mismatch for action=%q mode=%q: ActionSupportsMode=%v listContains=%v", spec.Name, mode, supported, seenInList)
			}
		}
	}
}
