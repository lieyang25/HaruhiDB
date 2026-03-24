package action

import (
	"encoding/json"
	"slices"
)

type ActionCategory string

const (
	ActionCategoryMetadata ActionCategory = "metadata"
	ActionCategoryRead     ActionCategory = "read"
	ActionCategoryWrite    ActionCategory = "write"
	ActionCategoryDDL      ActionCategory = "ddl"
	ActionCategoryBatch    ActionCategory = "batch"
)

type ActionSpec struct {
	Name         Action         `json:"name"`
	Category     ActionCategory `json:"category"`
	Public       bool           `json:"public"`
	Modes        []Mode         `json:"modes"`
	ArgsSchema   map[string]any `json:"args_schema"`
	Examples     []map[string]any `json:"examples"`
	IsFormAction bool           `json:"is_form_action"`
}

var actionRegistry = []ActionSpec{
	{
		Name:     ActionListTables,
		Category: ActionCategoryMetadata,
		Public:   true,
		Modes:    []Mode{ModeReadOnly, ModeReadWrite},
		ArgsSchema: map[string]any{
			"type":       "object",
			"properties": map[string]any{},
			"required":   []string{},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionListTables, ModeReadOnly, map[string]any{}),
		},
		IsFormAction: true,
	},
	{
		Name:     ActionTableExists,
		Category: ActionCategoryMetadata,
		Public:   true,
		Modes:    []Mode{ModeReadOnly, ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table": map[string]any{"type": "string"},
			},
			"required": []string{"table"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionTableExists, ModeReadOnly, map[string]any{"table": "users"}),
		},
		IsFormAction: false,
	},
	{
		Name:     ActionDescribeTable,
		Category: ActionCategoryMetadata,
		Public:   true,
		Modes:    []Mode{ModeReadOnly, ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table": map[string]any{"type": "string"},
			},
			"required": []string{"table"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionDescribeTable, ModeReadOnly, map[string]any{"table": "users"}),
		},
		IsFormAction: true,
	},
	{
		Name:     ActionGetByPrimaryInt,
		Category: ActionCategoryRead,
		Public:   true,
		Modes:    []Mode{ModeReadOnly, ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table": map[string]any{"type": "string"},
				"key":   map[string]any{"type": "integer"},
			},
			"required": []string{"table", "key"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionGetByPrimaryInt, ModeReadOnly, map[string]any{"table": "users", "key": 1}),
		},
		IsFormAction: false,
	},
	{
		Name:     ActionScanAll,
		Category: ActionCategoryRead,
		Public:   true,
		Modes:    []Mode{ModeReadOnly, ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table": map[string]any{"type": "string"},
				"limit": map[string]any{"type": "integer", "minimum": 1},
			},
			"required": []string{"table"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionScanAll, ModeReadOnly, map[string]any{"table": "users", "limit": 100}),
		},
		IsFormAction: false,
	},
	{
		Name:     ActionScanPrimaryIntRange,
		Category: ActionCategoryRead,
		Public:   true,
		Modes:    []Mode{ModeReadOnly, ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table":     map[string]any{"type": "string"},
				"start_key": map[string]any{"type": "integer"},
				"end_key":   map[string]any{"type": "integer"},
				"limit":     map[string]any{"type": "integer", "minimum": 1},
			},
			"required": []string{"table", "start_key", "end_key"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionScanPrimaryIntRange, ModeReadOnly, map[string]any{"table": "users", "start_key": 1, "end_key": 10, "limit": 100}),
		},
		IsFormAction: false,
	},
	{
		Name:     ActionInsertRow,
		Category: ActionCategoryWrite,
		Public:   true,
		Modes:    []Mode{ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table":  map[string]any{"type": "string"},
				"values": map[string]any{"type": "object", "description": "column->scalar; NULL and DECIMAL are not supported"},
			},
			"required": []string{"table", "values"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionInsertRow, ModeReadWrite, map[string]any{"table": "users", "values": map[string]any{"id": 1, "name": "alice"}}),
		},
		IsFormAction: false,
	},
	{
		Name:     ActionUpdateByPrimaryInt,
		Category: ActionCategoryWrite,
		Public:   true,
		Modes:    []Mode{ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table":  map[string]any{"type": "string"},
				"key":    map[string]any{"type": "integer"},
				"values": map[string]any{"type": "object", "description": "column->scalar; NULL and DECIMAL are not supported"},
			},
			"required": []string{"table", "key", "values"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionUpdateByPrimaryInt, ModeReadWrite, map[string]any{"table": "users", "key": 1, "values": map[string]any{"id": 1, "name": "alice-updated"}}),
		},
		IsFormAction: false,
	},
	{
		Name:     ActionDeleteByPrimaryInt,
		Category: ActionCategoryWrite,
		Public:   true,
		Modes:    []Mode{ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table": map[string]any{"type": "string"},
				"key":   map[string]any{"type": "integer"},
			},
			"required": []string{"table", "key"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionDeleteByPrimaryInt, ModeReadWrite, map[string]any{"table": "users", "key": 1}),
		},
		IsFormAction: false,
	},
	{
		Name:     ActionCreateTable,
		Category: ActionCategoryDDL,
		Public:   true,
		Modes:    []Mode{ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table": map[string]any{"type": "string"},
				"columns": map[string]any{
					"type": "array",
					"items": map[string]any{
						"type": "object",
						"properties": map[string]any{
							"name":     map[string]any{"type": "string"},
							"type":     map[string]any{"type": "string", "enum": []string{"BOOLEAN", "TINYINT", "SMALLINT", "INTEGER", "BIGINT", "FLOAT", "DOUBLE", "VARCHAR"}},
							"nullable": map[string]any{"type": "boolean"},
							"length":   map[string]any{"type": "integer", "minimum": 1},
						},
						"required": []string{"name", "type", "nullable"},
					},
				},
			},
			"required": []string{"table", "columns"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionCreateTable, ModeReadWrite, map[string]any{
				"table": "users",
				"columns": []map[string]any{
					{"name": "id", "type": "INTEGER", "nullable": false},
					{"name": "name", "type": "VARCHAR", "length": 64, "nullable": false},
				},
			}),
		},
		IsFormAction: true,
	},
	{
		Name:     ActionDropTable,
		Category: ActionCategoryDDL,
		Public:   true,
		Modes:    []Mode{ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table": map[string]any{"type": "string"},
			},
			"required": []string{"table"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionDropTable, ModeReadWrite, map[string]any{"table": "users"}),
		},
		IsFormAction: true,
	},
	{
		Name:     ActionCreatePrimaryIndex,
		Category: ActionCategoryDDL,
		Public:   true,
		Modes:    []Mode{ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table": map[string]any{"type": "string"},
				"index": map[string]any{"type": "string"},
			},
			"required": []string{"table", "index"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionCreatePrimaryIndex, ModeReadWrite, map[string]any{"table": "users", "index": "idx_users_id"}),
		},
		IsFormAction: false,
	},
	{
		Name:     ActionDropIndex,
		Category: ActionCategoryDDL,
		Public:   true,
		Modes:    []Mode{ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"table": map[string]any{"type": "string"},
				"index": map[string]any{"type": "string"},
			},
			"required": []string{"table", "index"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionDropIndex, ModeReadWrite, map[string]any{"table": "users", "index": "idx_users_id"}),
		},
		IsFormAction: false,
	},
	{
		Name:     ActionBatch,
		Category: ActionCategoryBatch,
		Public:   true,
		Modes:    []Mode{ModeReadOnly, ModeReadWrite},
		ArgsSchema: map[string]any{
			"type": "object",
			"properties": map[string]any{
				"requests": map[string]any{
					"type": "array",
					"items": map[string]any{
						"type": "object",
						"properties": map[string]any{
							"action": map[string]any{"type": "string"},
							"args":   map[string]any{"type": "object"},
						},
						"required": []string{"action", "args"},
					},
				},
				"stop_on_error": map[string]any{"type": "boolean"},
			},
			"required": []string{"requests"},
		},
		Examples: []map[string]any{
			makeEnvelopeExample(ActionBatch, ModeReadOnly, map[string]any{
				"stop_on_error": false,
				"requests": []map[string]any{
					{"action": "list_tables", "args": map[string]any{}},
					{"action": "describe_table", "args": map[string]any{"table": "users"}},
				},
			}),
		},
		IsFormAction: false,
	},
}

func makeEnvelopeExample(actionName Action, mode Mode, args map[string]any) map[string]any {
	return map[string]any{
		"version":    DefaultVersion,
		"request_id": "req-example",
		"mode":       mode,
		"action":     actionName,
		"args":       args,
	}
}

func PublicActionSpecs() []ActionSpec {
	return filterActionSpecs("", true)
}

func PublicActionSpecsForMode(mode Mode) []ActionSpec {
	return filterActionSpecs(mode, true)
}

func ActionSpecByName(name Action) (ActionSpec, bool) {
	for _, spec := range actionRegistry {
		if spec.Name == name {
			return cloneActionSpec(spec), true
		}
	}
	return ActionSpec{}, false
}

func IsPublicAction(name Action) bool {
	spec, ok := ActionSpecByName(name)
	return ok && spec.Public
}

func ActionSupportsMode(name Action, mode Mode) bool {
	spec, ok := ActionSpecByName(name)
	if !ok {
		return false
	}
	return slices.Contains(spec.Modes, mode)
}

func PublicActionNamesForMode(mode Mode) []string {
	specs := PublicActionSpecsForMode(mode)
	out := make([]string, 0, len(specs))
	for _, spec := range specs {
		out = append(out, string(spec.Name))
	}
	return out
}

func PublicActionNames() []string {
	specs := PublicActionSpecs()
	out := make([]string, 0, len(specs))
	for _, spec := range specs {
		out = append(out, string(spec.Name))
	}
	return out
}

func PublicActionSpecsByCategoryForMode(mode Mode) map[string][]ActionSpec {
	specs := PublicActionSpecsForMode(mode)
	grouped := map[string][]ActionSpec{}
	for _, spec := range specs {
		key := string(spec.Category)
		grouped[key] = append(grouped[key], spec)
	}
	return grouped
}

func filterActionSpecs(mode Mode, publicOnly bool) []ActionSpec {
	out := make([]ActionSpec, 0, len(actionRegistry))
	for _, spec := range actionRegistry {
		if publicOnly && !spec.Public {
			continue
		}
		if mode != "" && !slices.Contains(spec.Modes, mode) {
			continue
		}
		out = append(out, cloneActionSpec(spec))
	}
	return out
}

func cloneActionSpec(spec ActionSpec) ActionSpec {
	cloned := spec
	cloned.Modes = append([]Mode(nil), spec.Modes...)
	cloned.ArgsSchema = deepCopyMap(spec.ArgsSchema)
	cloned.Examples = deepCopyExamples(spec.Examples)
	return cloned
}

func deepCopyExamples(in []map[string]any) []map[string]any {
	if len(in) == 0 {
		return nil
	}
	out := make([]map[string]any, 0, len(in))
	for _, item := range in {
		out = append(out, deepCopyMap(item))
	}
	return out
}

func deepCopyMap(in map[string]any) map[string]any {
	if len(in) == 0 {
		return map[string]any{}
	}
	raw, err := json.Marshal(in)
	if err != nil {
		return map[string]any{}
	}
	var out map[string]any
	if err := json.Unmarshal(raw, &out); err != nil {
		return map[string]any{}
	}
	return out
}
