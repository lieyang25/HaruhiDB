(function () {
  const STORAGE_ACTIVE_DB_PATH = "active_db_path";
  const STORAGE_RECENT_DB_PATHS = "recent_db_paths";
  const MAX_RECENT_PATHS = 8;

  const WRITE_ACTIONS = new Set([
    "insert_row",
    "update_by_primary_int",
    "delete_by_primary_int",
    "create_table",
    "drop_table",
    "create_primary_int_index",
    "drop_index"
  ]);

  const INTEGER_TYPES = new Set(["TINYINT", "SMALLINT", "INTEGER", "BIGINT"]);
  const NUMBER_TYPES = new Set(["FLOAT", "DOUBLE"]);
  const COLUMN_TYPES = ["BOOLEAN", "TINYINT", "SMALLINT", "INTEGER", "BIGINT", "FLOAT", "DOUBLE", "VARCHAR"];
  const VALUE_TYPE_OPTIONS = ["string", "integer", "number", "boolean"];

  const statusEl = document.getElementById("status");
  const capabilitiesInfoEl = document.getElementById("capabilitiesInfo");

  const dbPathInputEl = document.getElementById("dbPathInput");
  const switchDbBtn = document.getElementById("switchDbBtn");
  const recentDbSelectEl = document.getElementById("recentDbSelect");
  const useRecentBtn = document.getElementById("useRecentBtn");
  const dbShortInfoEl = document.getElementById("dbShortInfo");
  const dbFullPathEl = document.getElementById("dbFullPath");
  const dbDefaultPathEl = document.getElementById("dbDefaultPath");

  const listTablesBtn = document.getElementById("listTablesBtn");
  const manageTableSelectEl = document.getElementById("manageTableSelect");
  const tableExistsBtn = document.getElementById("tableExistsBtn");
  const describeTableBtn = document.getElementById("describeTableBtn");
  const dropTableBtn = document.getElementById("dropTableBtn");
  const indexNameInputEl = document.getElementById("indexNameInput");
  const createIndexBtn = document.getElementById("createIndexBtn");
  const dropIndexBtn = document.getElementById("dropIndexBtn");

  const createTableNameEl = document.getElementById("createTableName");
  const createColumnsBodyEl = document.getElementById("createColumnsBody");
  const addCreateColumnBtn = document.getElementById("addCreateColumnBtn");
  const createTableBtn = document.getElementById("createTableBtn");

  const queryTableSelectEl = document.getElementById("queryTableSelect");
  const queryLimitInputEl = document.getElementById("queryLimitInput");
  const queryFilterModeEl = document.getElementById("queryFilterMode");
  const queryExactRowEl = document.getElementById("queryExactRow");
  const queryRangeRowEl = document.getElementById("queryRangeRow");
  const queryExactKeyInputEl = document.getElementById("queryExactKeyInput");
  const queryStartKeyInputEl = document.getElementById("queryStartKeyInput");
  const queryEndKeyInputEl = document.getElementById("queryEndKeyInput");
  const runQueryBtn = document.getElementById("runQueryBtn");
  const refreshQueryBtn = document.getElementById("refreshQueryBtn");

  const insertTableSelectEl = document.getElementById("insertTableSelect");
  const insertFieldsEl = document.getElementById("insertFields");
  const insertRowBtn = document.getElementById("insertRowBtn");

  const updateTableSelectEl = document.getElementById("updateTableSelect");
  const updateKeyInputEl = document.getElementById("updateKeyInput");
  const updateFieldsEl = document.getElementById("updateFields");
  const updateRowBtn = document.getElementById("updateRowBtn");
  const deleteKeyInputEl = document.getElementById("deleteKeyInput");
  const deleteRowBtn = document.getElementById("deleteRowBtn");

  const batchModeSelectEl = document.getElementById("batchModeSelect");
  const batchStopOnErrorInputEl = document.getElementById("batchStopOnErrorInput");
  const batchStepsEl = document.getElementById("batchSteps");
  const addBatchStepBtn = document.getElementById("addBatchStepBtn");
  const runBatchBtn = document.getElementById("runBatchBtn");

  const actionSelectEl = document.getElementById("actionSelect");
  const modeSelectEl = document.getElementById("modeSelect");
  const argsInputEl = document.getElementById("argsInput");
  const buildEnvelopeBtn = document.getElementById("buildEnvelopeBtn");
  const runEnvelopeBtn = document.getElementById("runEnvelopeBtn");

  const resultSummaryEl = document.getElementById("resultSummary");
  const resultViewEl = document.getElementById("resultView");
  const envelopeOutEl = document.getElementById("envelopeOut");
  const rawOutEl = document.getElementById("rawOut");

  const state = {
    activeDBPath: "",
    defaultDBPath: "",
    tables: [],
    capabilities: null,
    tableSchemaCache: {},
    batchSteps: [],
    batchStepCounter: 1
  };

  const busyButtons = [
    switchDbBtn,
    useRecentBtn,
    listTablesBtn,
    tableExistsBtn,
    describeTableBtn,
    dropTableBtn,
    createIndexBtn,
    dropIndexBtn,
    addCreateColumnBtn,
    createTableBtn,
    runQueryBtn,
    refreshQueryBtn,
    insertRowBtn,
    updateRowBtn,
    deleteRowBtn,
    addBatchStepBtn,
    runBatchBtn,
    buildEnvelopeBtn,
    runEnvelopeBtn
  ];

  function setBusy(busy) {
    busyButtons.forEach(function (btn) {
      if (btn) {
        btn.disabled = busy;
      }
    });
  }

  function setStatus(text, isError) {
    statusEl.textContent = text;
    statusEl.style.color = isError ? "#b42318" : "#0b5d57";
  }

  function fireAndForget(promise) {
    promise.catch(function () {
      return;
    });
  }

  function pretty(value) {
    try {
      return JSON.stringify(value, null, 2);
    } catch (_e) {
      return String(value);
    }
  }

  async function requestJSON(url, options) {
    const response = await fetch(url, options);

    let parsed;
    try {
      parsed = await response.json();
    } catch (_e) {
      parsed = { error: { message: "response is not valid JSON" } };
    }

    if (!response.ok) {
      const message = parsed && parsed.error && parsed.error.message ? parsed.error.message : "request failed";
      const err = new Error(message);
      err.payload = parsed;
      throw err;
    }

    return parsed;
  }

  async function postJSON(url, payload) {
    return requestJSON(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload)
    });
  }

  async function getJSON(url) {
    return requestJSON(url, { method: "GET" });
  }

  function newRequestID(prefix) {
    return prefix + "-" + Date.now();
  }

  function pathBaseName(path) {
    const text = (path || "").trim();
    if (!text) {
      return "";
    }
    const normalized = text.replace(/\\/g, "/");
    const parts = normalized.split("/").filter(Boolean);
    return parts.length > 0 ? parts[parts.length - 1] : normalized;
  }

  function defaultIndexName(table) {
    const t = (table || "").trim();
    if (!t) {
      return "";
    }
    return "idx_" + t + "_id";
  }

  function normalizeColumnType(typeName) {
    return String(typeName || "").trim().toUpperCase();
  }

  function ensureText(valueText, label) {
    const text = String(valueText || "").trim();
    if (!text) {
      throw new Error(label + "不能为空");
    }
    return text;
  }

  function parseInteger(valueText, label) {
    const text = String(valueText || "").trim();
    if (!text) {
      throw new Error(label + "不能为空");
    }
    const value = Number(text);
    if (!Number.isFinite(value) || !Number.isInteger(value)) {
      throw new Error(label + "必须是整数");
    }
    return value;
  }

  function parsePositiveInteger(valueText, label) {
    const value = parseInteger(valueText, label);
    if (value <= 0) {
      throw new Error(label + "必须大于 0");
    }
    return value;
  }

  function readRecentDBPaths() {
    try {
      const raw = localStorage.getItem(STORAGE_RECENT_DB_PATHS);
      if (!raw) {
        return [];
      }
      const parsed = JSON.parse(raw);
      if (!Array.isArray(parsed)) {
        return [];
      }
      const result = [];
      parsed.forEach(function (item) {
        if (typeof item !== "string") {
          return;
        }
        const trimmed = item.trim();
        if (!trimmed) {
          return;
        }
        if (result.indexOf(trimmed) >= 0) {
          return;
        }
        result.push(trimmed);
      });
      return result.slice(0, MAX_RECENT_PATHS);
    } catch (_e) {
      return [];
    }
  }

  function writeRecentDBPaths(paths) {
    localStorage.setItem(STORAGE_RECENT_DB_PATHS, JSON.stringify(paths.slice(0, MAX_RECENT_PATHS)));
  }

  function rememberDBPath(path) {
    const trimmed = (path || "").trim();
    if (!trimmed) {
      return;
    }

    localStorage.setItem(STORAGE_ACTIVE_DB_PATH, trimmed);

    const current = readRecentDBPaths();
    const next = [trimmed];
    current.forEach(function (item) {
      if (item !== trimmed) {
        next.push(item);
      }
    });
    writeRecentDBPaths(next);
  }

  function renderRecentDBPaths() {
    const paths = readRecentDBPaths();
    recentDbSelectEl.innerHTML = "";

    if (paths.length === 0) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "暂无最近路径";
      recentDbSelectEl.appendChild(option);
      return;
    }

    paths.forEach(function (path) {
      const option = document.createElement("option");
      option.value = path;
      option.textContent = path;
      if (path === state.activeDBPath) {
        option.selected = true;
      }
      recentDbSelectEl.appendChild(option);
    });
  }

  function updateDBInfo() {
    if (!state.activeDBPath) {
      dbShortInfoEl.textContent = "当前数据库: (未连接)";
      dbShortInfoEl.title = "";
      dbFullPathEl.textContent = "-";
      dbDefaultPathEl.textContent = "-";
      return;
    }

    const shortName = pathBaseName(state.activeDBPath) || state.activeDBPath;
    dbShortInfoEl.textContent = "当前数据库: " + shortName;
    dbShortInfoEl.title = state.activeDBPath;

    dbFullPathEl.textContent = state.activeDBPath;
    dbDefaultPathEl.textContent = state.defaultDBPath || "-";
  }

  function setSelectOptions(selectEl, values, emptyLabel) {
    const previous = selectEl.value;
    selectEl.innerHTML = "";

    if (!Array.isArray(values) || values.length === 0) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = emptyLabel || "(无可选项)";
      selectEl.appendChild(option);
      selectEl.disabled = true;
      return;
    }

    values.forEach(function (value) {
      const option = document.createElement("option");
      option.value = value;
      option.textContent = value;
      selectEl.appendChild(option);
    });

    if (previous && values.indexOf(previous) >= 0) {
      selectEl.value = previous;
    }

    selectEl.disabled = false;
  }

  function renderAllTableSelectors() {
    const tableList = Array.isArray(state.tables) ? state.tables : [];
    setSelectOptions(manageTableSelectEl, tableList, "(无表)");
    setSelectOptions(queryTableSelectEl, tableList, "(无表)");
    setSelectOptions(insertTableSelectEl, tableList, "(无表)");
    setSelectOptions(updateTableSelectEl, tableList, "(无表)");
  }

  function renderCapabilitiesSummary() {
    if (!state.capabilities) {
      capabilitiesInfoEl.textContent = "能力未加载。";
      return;
    }

    const roInfo = state.capabilities.actions_by_mode && state.capabilities.actions_by_mode.read_only;
    const rwInfo = state.capabilities.actions_by_mode && state.capabilities.actions_by_mode.read_write;
    const roCount = roInfo && Array.isArray(roInfo.names) ? roInfo.names.length : 0;
    const rwCount = rwInfo && Array.isArray(rwInfo.names) ? rwInfo.names.length : 0;

    capabilitiesInfoEl.textContent = "协议版本: " + state.capabilities.protocol_version +
      " | read_only: " + roCount +
      " | read_write: " + rwCount +
      " | 当前表: " + state.tables.length;
  }

  function buildCapabilitiesURL(dbPath) {
    const trimmed = (dbPath || "").trim();
    if (!trimmed) {
      return "/v1/capabilities";
    }
    return "/v1/capabilities?db_path=" + encodeURIComponent(trimmed);
  }

  function actionSupported(actionName, mode) {
    if (!state.capabilities || !state.capabilities.actions_by_mode) {
      return false;
    }
    const modeInfo = state.capabilities.actions_by_mode[mode];
    if (!modeInfo || !Array.isArray(modeInfo.names)) {
      return false;
    }
    return modeInfo.names.indexOf(actionName) >= 0;
  }

  function getAllActionSpecs() {
    if (!state.capabilities || !Array.isArray(state.capabilities.actions)) {
      return [];
    }
    return state.capabilities.actions.slice();
  }

  function getBatchActionSpecs() {
    return getAllActionSpecs().filter(function (spec) {
      return spec && spec.name && spec.name !== "batch";
    });
  }

  function getBatchActionNames() {
    return getBatchActionSpecs().map(function (spec) {
      return spec.name;
    });
  }

  function renderAvailability() {
    const hasTables = state.tables && state.tables.length > 0;

    listTablesBtn.disabled = !actionSupported("list_tables", "read_only");
    tableExistsBtn.disabled = !hasTables || !actionSupported("table_exists", "read_only");
    describeTableBtn.disabled = !hasTables || !actionSupported("describe_table", "read_only");
    dropTableBtn.disabled = !hasTables || !actionSupported("drop_table", "read_write");
    createIndexBtn.disabled = !hasTables || !actionSupported("create_primary_int_index", "read_write");
    dropIndexBtn.disabled = !hasTables || !actionSupported("drop_index", "read_write");

    createTableBtn.disabled = !actionSupported("create_table", "read_write");

    runQueryBtn.disabled = !hasTables;
    refreshQueryBtn.disabled = !hasTables;

    insertRowBtn.disabled = !hasTables || !actionSupported("insert_row", "read_write");
    updateRowBtn.disabled = !hasTables || !actionSupported("update_by_primary_int", "read_write");
    deleteRowBtn.disabled = !hasTables || !actionSupported("delete_by_primary_int", "read_write");

    addBatchStepBtn.disabled = !actionSupported("batch", "read_only") && !actionSupported("batch", "read_write");
    runBatchBtn.disabled = state.batchSteps.length === 0 || (!actionSupported("batch", "read_only") && !actionSupported("batch", "read_write"));
  }

  function defaultArgsForAction(actionName) {
    const table = state.tables && state.tables.length > 0 ? state.tables[0] : "users";
    switch (actionName) {
      case "list_tables":
        return {};
      case "table_exists":
      case "describe_table":
      case "drop_table":
        return { table: table };
      case "get_by_primary_int":
      case "delete_by_primary_int":
        return { table: table, key: 1 };
      case "scan_all":
        return { table: table, limit: 20 };
      case "scan_primary_int_range":
        return { table: table, start_key: 1, end_key: 100, limit: 20 };
      case "insert_row":
        return { table: table, values: { id: 1, name: "demo" } };
      case "update_by_primary_int":
        return { table: table, key: 1, values: { name: "demo-updated" } };
      case "create_table":
        return {
          table: "users_new",
          columns: [
            { name: "id", type: "INTEGER", nullable: false },
            { name: "name", type: "VARCHAR", length: 64, nullable: false }
          ]
        };
      case "create_primary_int_index":
      case "drop_index":
        return { table: table, index: defaultIndexName(table) };
      case "batch":
        return {
          stop_on_error: true,
          requests: [
            { action: "list_tables", args: {} },
            { action: "describe_table", args: { table: table } }
          ]
        };
      default:
        return {};
    }
  }

  function renderDebugActionOptions() {
    const previous = actionSelectEl.value;
    actionSelectEl.innerHTML = "";

    const actions = getAllActionSpecs();

    if (actions.length === 0) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "(无动作可用)";
      actionSelectEl.appendChild(option);
      argsInputEl.value = "{}";
      return;
    }

    actions.forEach(function (spec) {
      const option = document.createElement("option");
      option.value = spec.name;
      const modes = Array.isArray(spec.modes) ? spec.modes.join(",") : "read_only/read_write";
      option.textContent = spec.name + " [" + modes + "]";
      actionSelectEl.appendChild(option);
    });

    if (previous) {
      actionSelectEl.value = previous;
      if (!actionSelectEl.value) {
        actionSelectEl.selectedIndex = 0;
      }
    }

    syncDebugActionPreset(true);
  }

  function renderQueryFilterRows() {
    const mode = queryFilterModeEl.value;
    queryExactRowEl.style.display = mode === "key_exact" ? "flex" : "none";
    queryRangeRowEl.style.display = mode === "key_range" ? "flex" : "none";
  }

  function buildEnvelope(actionName, mode, args, prefix) {
    return {
      version: "v3",
      request_id: newRequestID(prefix || "ui"),
      mode: mode,
      action: actionName,
      args: args
    };
  }

  function copyEnvelopeToPayload(envelope) {
    const payload = Object.assign({}, envelope);
    if (state.activeDBPath) {
      payload.db_path = state.activeDBPath;
    }
    return payload;
  }

  function toCellText(value) {
    if (value === null || value === undefined) {
      return "null";
    }
    if (typeof value === "object") {
      return pretty(value);
    }
    return String(value);
  }

  function makeTableFromRows(rows) {
    const table = document.createElement("table");
    table.className = "result-table";

    if (!Array.isArray(rows) || rows.length === 0) {
      const tbody = document.createElement("tbody");
      const tr = document.createElement("tr");
      const td = document.createElement("td");
      td.textContent = "暂无数据";
      tr.appendChild(td);
      tbody.appendChild(tr);
      table.appendChild(tbody);
      return table;
    }

    const columns = [];
    rows.forEach(function (row) {
      if (!row || typeof row !== "object") {
        return;
      }
      Object.keys(row).forEach(function (key) {
        if (columns.indexOf(key) < 0) {
          columns.push(key);
        }
      });
    });

    const thead = document.createElement("thead");
    const hr = document.createElement("tr");
    columns.forEach(function (key) {
      const th = document.createElement("th");
      th.textContent = key;
      hr.appendChild(th);
    });
    thead.appendChild(hr);

    const tbody = document.createElement("tbody");
    rows.forEach(function (row) {
      const tr = document.createElement("tr");
      columns.forEach(function (key) {
        const td = document.createElement("td");
        td.textContent = toCellText(row ? row[key] : null);
        tr.appendChild(td);
      });
      tbody.appendChild(tr);
    });

    table.appendChild(thead);
    table.appendChild(tbody);
    return table;
  }

  function mapFriendlyError(code, message, actionName) {
    const raw = String(message || "");
    if (code === "CONSTRAINT") {
      if (raw.indexOf("must be empty for action") >= 0) {
        return "该动作要求目标表为空，请先清空数据后再执行。";
      }
      if (raw.indexOf("expected BOOLEAN") >= 0) {
        return "字段类型不匹配，目标列需要 BOOLEAN。";
      }
      return "约束校验失败，请检查索引、主键和字段类型。";
    }
    if (code === "INVALID_REQUEST") {
      return "请求参数不合法，请检查表名、字段和输入值。";
    }
    if (code === "NOT_FOUND") {
      return "目标对象不存在，请确认表、索引或记录是否存在。";
    }
    if (code === "INTERNAL") {
      if (actionName === "insert_row") {
        return "插入失败，请先检查表结构与输入字段是否匹配。";
      }
      return "执行失败，服务返回内部错误。";
    }
    if (code === "IO") {
      return "存储层读写失败，请检查数据库文件状态。";
    }
    return "动作执行失败。";
  }

  function renderErrorMessage(code, message, actionName) {
    const box = document.createElement("div");
    box.className = "result-error";

    const title = document.createElement("p");
    title.className = "result-error-main";
    title.textContent = mapFriendlyError(code, message, actionName);

    const detail = document.createElement("p");
    detail.className = "result-error-detail";
    detail.textContent = "[" + code + "] " + message;

    box.appendChild(title);
    box.appendChild(detail);
    return box;
  }

  function renderSuccessMessage(text) {
    const div = document.createElement("div");
    div.className = "result-success";
    div.textContent = text;
    return div;
  }

  function summarizeSuccess(actionName, data) {
    if (data && typeof data === "object") {
      if (typeof data.exists === "boolean") {
        return data.exists ? "目标表存在。" : "目标表不存在。";
      }
      if (typeof data.created === "number") {
        if (actionName === "create_table") {
          return "建表成功。";
        }
        if (actionName === "create_primary_int_index") {
          return "创建索引成功。";
        }
      }
      if (typeof data.dropped === "number") {
        if (actionName === "drop_table") {
          return "删除表成功。";
        }
        if (actionName === "drop_index") {
          return "删除索引成功。";
        }
      }
      if (typeof data.inserted === "number") {
        return "插入成功。";
      }
      if (typeof data.updated === "number") {
        return "更新完成。";
      }
      if (typeof data.deleted === "number") {
        return "删除完成。";
      }
      if (Array.isArray(data.tables)) {
        return "已列出 " + data.tables.length + " 张表。";
      }
      if (Array.isArray(data.rows)) {
        return "查询完成，共 " + (typeof data.row_count === "number" ? data.row_count : data.rows.length) + " 行。";
      }
      if (Object.prototype.hasOwnProperty.call(data, "found")) {
        return data.found ? "已找到记录。" : "未找到记录。";
      }
      if (Array.isArray(data.results)) {
        const successCount = typeof data.succeeded === "number" ? data.succeeded : 0;
        const failCount = typeof data.failed === "number" ? data.failed : 0;
        return "批量执行完成：成功 " + successCount + "，失败 " + failCount + "。";
      }
    }
    return "执行成功。";
  }

  function renderResultView(actionName, response) {
    resultViewEl.innerHTML = "";

    if (!response || typeof response !== "object") {
      resultSummaryEl.textContent = "响应异常。";
      resultViewEl.appendChild(renderErrorMessage("UNKNOWN", "服务返回了非 JSON 响应", actionName));
      return;
    }

    if (!response.ok) {
      const code = response.error && response.error.code ? response.error.code : "UNKNOWN";
      const message = response.error && response.error.message ? response.error.message : "unknown error";
      resultSummaryEl.textContent = "执行失败。";
      resultViewEl.appendChild(renderErrorMessage(code, message, actionName));
      return;
    }

    const data = response.data || {};
    resultSummaryEl.textContent = summarizeSuccess(actionName, data);

    if (Array.isArray(data.rows)) {
      resultViewEl.appendChild(makeTableFromRows(data.rows));
      return;
    }

    if (data.row && typeof data.row === "object") {
      resultViewEl.appendChild(makeTableFromRows([data.row]));
      return;
    }

    if (Array.isArray(data.columns)) {
      const describeRows = data.columns.map(function (column) {
        return {
          name: column.name,
          type: column.type,
          length: column.length || "",
          nullable: column.nullable
        };
      });
      resultViewEl.appendChild(makeTableFromRows(describeRows));

      if (Array.isArray(data.indexes)) {
        const indexNames = data.indexes.map(function (item) { return item.name; }).join(", ");
        const note = document.createElement("p");
        note.className = "subtle";
        note.textContent = "索引: " + (indexNames || "(无)");
        resultViewEl.appendChild(note);
      }
      return;
    }

    if (Array.isArray(data.tables)) {
      const rows = data.tables.map(function (name) {
        return { table: name };
      });
      resultViewEl.appendChild(makeTableFromRows(rows));
      return;
    }

    if (Array.isArray(data.results)) {
      const rows = data.results.map(function (item) {
        const code = item.error && item.error.code ? item.error.code : "";
        const err = item.error && item.error.message ? item.error.message : "";
        return {
          index: item.index,
          action: item.action,
          ok: item.ok,
          code: code,
          message: err
        };
      });
      resultViewEl.appendChild(makeTableFromRows(rows));
      return;
    }

    if (typeof data.exists === "boolean") {
      resultViewEl.appendChild(renderSuccessMessage(data.exists ? "表存在。" : "表不存在。"));
      return;
    }

    if (typeof data.found === "boolean" && data.row == null) {
      resultViewEl.appendChild(renderSuccessMessage(data.found ? "已找到记录。" : "未找到记录。"));
      return;
    }

    const keys = Object.keys(data);
    if (keys.length > 0) {
      const row = {};
      keys.forEach(function (key) {
        row[key] = data[key];
      });
      resultViewEl.appendChild(makeTableFromRows([row]));
      return;
    }

    const empty = document.createElement("div");
    empty.className = "empty-result";
    empty.textContent = "执行完成，当前动作没有可展示的数据。";
    resultViewEl.appendChild(empty);
  }

  async function runEnvelope(envelope) {
    envelopeOutEl.textContent = pretty(envelope);

    const payload = copyEnvelopeToPayload(envelope);
    setBusy(true);
    setStatus("执行中...", false);

    try {
      const result = await postJSON("/v1/action", payload);
      rawOutEl.textContent = pretty(result);
      renderResultView(envelope.action, result);
      setStatus(result.ok ? "执行完成。" : "执行失败。", !result.ok);

      if (result.ok && (WRITE_ACTIONS.has(envelope.action) || envelope.action === "batch")) {
        state.tableSchemaCache = {};
        await loadCapabilities(state.activeDBPath, true);
      }

      return result;
    } catch (err) {
      const payloadError = err && err.payload ? err.payload : {
        ok: false,
        error: { message: err && err.message ? err.message : "unknown error" }
      };
      rawOutEl.textContent = pretty(payloadError);
      renderResultView(envelope.action, payloadError);
      setStatus("失败: " + (err && err.message ? err.message : "unknown error"), true);
      throw err;
    } finally {
      setBusy(false);
      renderAvailability();
    }
  }

  async function executeAction(actionName, args, mode, prefix) {
    const envelope = buildEnvelope(actionName, mode, args, prefix);
    return runEnvelope(envelope);
  }

  async function loadCapabilities(dbPath, keepStatusText) {
    const payload = await getJSON(buildCapabilitiesURL(dbPath));

    state.capabilities = payload;
    state.activeDBPath = (payload.db_path || "").trim();
    state.defaultDBPath = (payload.default_db_path || "").trim();
    state.tables = Array.isArray(payload.tables) ? payload.tables.slice() : [];

    if (state.activeDBPath) {
      dbPathInputEl.value = state.activeDBPath;
      rememberDBPath(state.activeDBPath);
    }

    renderRecentDBPaths();
    renderAllTableSelectors();
    renderCapabilitiesSummary();
    renderDebugActionOptions();
    updateDBInfo();

    syncIndexName(true);
    refreshBatchActionOptions();

    if (state.tables.length > 0) {
      await refreshInsertFields(true);
      await refreshUpdateFields(true);
    } else {
      renderValueFields(insertFieldsEl, [], "insert", "");
      renderValueFields(updateFieldsEl, [], "update", "");
    }

    if (!keepStatusText) {
      setStatus("能力已加载，当前数据库已就绪。", false);
    }

    renderAvailability();
  }

  function appendColumnRow(bodyEl, initial) {
    const tr = document.createElement("tr");

    const nameTd = document.createElement("td");
    const nameInput = document.createElement("input");
    nameInput.type = "text";
    nameInput.className = "create-col-name";
    nameInput.placeholder = "列名";
    if (initial && typeof initial.name === "string") {
      nameInput.value = initial.name;
    }
    nameTd.appendChild(nameInput);

    const typeTd = document.createElement("td");
    const typeSelect = document.createElement("select");
    typeSelect.className = "create-col-type";
    COLUMN_TYPES.forEach(function (typeName) {
      const option = document.createElement("option");
      option.value = typeName;
      option.textContent = typeName;
      typeSelect.appendChild(option);
    });
    if (initial && typeof initial.type === "string") {
      typeSelect.value = normalizeColumnType(initial.type);
    }
    typeTd.appendChild(typeSelect);

    const lengthTd = document.createElement("td");
    const lengthInput = document.createElement("input");
    lengthInput.type = "number";
    lengthInput.className = "create-col-length";
    lengthInput.min = "1";
    lengthInput.step = "1";
    lengthInput.placeholder = "VARCHAR";
    if (initial && typeof initial.length === "number") {
      lengthInput.value = String(initial.length);
    }
    lengthTd.appendChild(lengthInput);

    const nullableTd = document.createElement("td");
    const nullableInput = document.createElement("input");
    nullableInput.type = "checkbox";
    nullableInput.className = "create-col-nullable";
    if (initial && typeof initial.nullable === "boolean") {
      nullableInput.checked = initial.nullable;
    }
    nullableTd.appendChild(nullableInput);

    const opTd = document.createElement("td");
    const removeBtn = document.createElement("button");
    removeBtn.type = "button";
    removeBtn.className = "ghost remove-col-btn";
    removeBtn.textContent = "删除";
    removeBtn.addEventListener("click", function () {
      tr.remove();
      if (bodyEl.children.length === 0) {
        appendColumnRow(bodyEl);
      }
    });
    opTd.appendChild(removeBtn);

    function syncLengthInput() {
      const isVarchar = typeSelect.value === "VARCHAR";
      lengthInput.disabled = !isVarchar;
      if (!isVarchar) {
        lengthInput.value = "";
      }
    }

    typeSelect.addEventListener("change", syncLengthInput);
    syncLengthInput();

    tr.appendChild(nameTd);
    tr.appendChild(typeTd);
    tr.appendChild(lengthTd);
    tr.appendChild(nullableTd);
    tr.appendChild(opTd);

    bodyEl.appendChild(tr);
  }

  function collectColumnsFromBody(bodyEl, labelPrefix) {
    const rows = bodyEl.querySelectorAll("tr");
    if (!rows || rows.length === 0) {
      throw new Error((labelPrefix || "列定义") + "至少需要 1 列");
    }

    const columns = [];
    const seen = {};

    for (let i = 0; i < rows.length; i++) {
      const row = rows[i];
      const name = ensureText(row.querySelector(".create-col-name").value, "第 " + (i + 1) + " 行列名");
      const type = normalizeColumnType(row.querySelector(".create-col-type").value);
      const nullable = !!row.querySelector(".create-col-nullable").checked;
      const lengthText = (row.querySelector(".create-col-length").value || "").trim();

      if (seen[name]) {
        throw new Error("列名重复: " + name);
      }
      seen[name] = true;

      const column = {
        name: name,
        type: type,
        nullable: nullable
      };

      if (type === "VARCHAR") {
        column.length = parsePositiveInteger(lengthText, "第 " + (i + 1) + " 行 VARCHAR 长度");
      }

      columns.push(column);
    }

    return columns;
  }

  function createValueInput(columnType, columnLength) {
    const type = normalizeColumnType(columnType);

    if (type === "BOOLEAN") {
      const boolWrap = document.createElement("label");
      boolWrap.className = "insert-boolean";

      const boolInput = document.createElement("input");
      boolInput.type = "checkbox";
      boolInput.className = "value-input";

      boolWrap.appendChild(boolInput);
      boolWrap.appendChild(document.createTextNode("true / false"));

      return {
        container: boolWrap,
        input: boolInput,
        readValue: function () {
          return !!boolInput.checked;
        },
        setDisabled: function (disabled) {
          boolInput.disabled = disabled;
        }
      };
    }

    const input = document.createElement("input");
    input.className = "value-input";

    if (INTEGER_TYPES.has(type)) {
      input.type = "number";
      input.step = "1";
    } else if (NUMBER_TYPES.has(type)) {
      input.type = "number";
      input.step = "any";
    } else {
      input.type = "text";
      if (type === "VARCHAR" && typeof columnLength === "number" && columnLength > 0) {
        input.maxLength = columnLength;
        input.placeholder = "最长 " + columnLength + " 字符";
      }
    }

    return {
      container: input,
      input: input,
      readValue: function (label) {
        const raw = (input.value || "").trim();

        if (INTEGER_TYPES.has(type)) {
          return parseInteger(raw, label);
        }

        if (NUMBER_TYPES.has(type)) {
          if (!raw) {
            throw new Error(label + "不能为空");
          }
          const number = Number(raw);
          if (!Number.isFinite(number)) {
            throw new Error(label + "必须是数字");
          }
          return number;
        }

        return input.value || "";
      },
      setDisabled: function (disabled) {
        input.disabled = disabled;
      }
    };
  }

  function renderValueFields(container, columns, mode, tableName) {
    container.innerHTML = "";

    if (!tableName) {
      const empty = document.createElement("div");
      empty.className = "empty-result";
      empty.textContent = "请选择表后再操作。";
      container.appendChild(empty);
      return;
    }

    if (!Array.isArray(columns) || columns.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-result";
      empty.textContent = "未获取到列结构，请先点击“查看结构”确认。";
      container.appendChild(empty);
      return;
    }

    columns.forEach(function (column) {
      const wrapper = document.createElement("div");
      wrapper.className = "insert-field";
      wrapper.dataset.columnName = column.name;
      wrapper.dataset.columnType = normalizeColumnType(column.type);

      const label = document.createElement("label");
      label.textContent = column.name + " (" + wrapper.dataset.columnType + ")";
      wrapper.appendChild(label);

      let includeToggle = null;
      if (mode === "update") {
        const toggleLabel = document.createElement("label");
        toggleLabel.className = "checkbox-inline";

        includeToggle = document.createElement("input");
        includeToggle.type = "checkbox";
        includeToggle.className = "update-include-toggle";

        const text = document.createElement("span");
        text.textContent = "更新该字段";

        toggleLabel.appendChild(includeToggle);
        toggleLabel.appendChild(text);
        wrapper.appendChild(toggleLabel);
      }

      const inputFactory = createValueInput(wrapper.dataset.columnType, column.length);
      inputFactory.container.classList.add("insert-input");
      wrapper.appendChild(inputFactory.container);

      if (includeToggle) {
        inputFactory.setDisabled(true);
        includeToggle.addEventListener("change", function () {
          inputFactory.setDisabled(!includeToggle.checked);
        });
      }

      wrapper._inputFactory = inputFactory;
      wrapper._includeToggle = includeToggle;
      container.appendChild(wrapper);
    });
  }

  function collectValuesFromFields(container, mode) {
    const wrappers = container.querySelectorAll(".insert-field");
    if (!wrappers || wrappers.length === 0) {
      throw new Error("当前表没有可编辑列");
    }

    const values = {};
    let touchedCount = 0;

    wrappers.forEach(function (wrapper) {
      const name = wrapper.dataset.columnName;
      const inputFactory = wrapper._inputFactory;
      const includeToggle = wrapper._includeToggle;

      if (!name || !inputFactory) {
        return;
      }

      if (mode === "update") {
        if (!includeToggle || !includeToggle.checked) {
          return;
        }
      }

      const label = "列 " + name;
      const value = typeof inputFactory.readValue === "function"
        ? inputFactory.readValue(label)
        : null;

      values[name] = value;
      touchedCount += 1;
    });

    if (mode === "update" && touchedCount === 0) {
      throw new Error("请至少勾选 1 个需要更新的字段");
    }

    return values;
  }

  async function loadTableSchema(table) {
    const name = (table || "").trim();
    if (!name) {
      return null;
    }
    if (state.tableSchemaCache[name]) {
      return state.tableSchemaCache[name];
    }

    const envelope = buildEnvelope("describe_table", "read_only", { table: name }, "schema");
    const payload = copyEnvelopeToPayload(envelope);
    const response = await postJSON("/v1/action", payload);

    if (!response.ok || !response.data || !Array.isArray(response.data.columns)) {
      throw new Error("读取表结构失败");
    }

    state.tableSchemaCache[name] = response.data;
    return response.data;
  }

  async function refreshInsertFields(silentOnError) {
    const table = (insertTableSelectEl.value || "").trim();
    if (!table) {
      renderValueFields(insertFieldsEl, [], "insert", "");
      return;
    }

    try {
      const schema = await loadTableSchema(table);
      renderValueFields(insertFieldsEl, schema.columns || [], "insert", table);
    } catch (err) {
      renderValueFields(insertFieldsEl, [], "insert", table);
      if (!silentOnError) {
        setStatus("读取插入表单结构失败: " + (err && err.message ? err.message : "unknown error"), true);
      }
    }
  }

  async function refreshUpdateFields(silentOnError) {
    const table = (updateTableSelectEl.value || "").trim();
    if (!table) {
      renderValueFields(updateFieldsEl, [], "update", "");
      return;
    }

    try {
      const schema = await loadTableSchema(table);
      renderValueFields(updateFieldsEl, schema.columns || [], "update", table);
    } catch (err) {
      renderValueFields(updateFieldsEl, [], "update", table);
      if (!silentOnError) {
        setStatus("读取更新表单结构失败: " + (err && err.message ? err.message : "unknown error"), true);
      }
    }
  }

  function collectInsertValues() {
    const table = ensureText(insertTableSelectEl.value, "插入表名");
    return {
      table: table,
      values: collectValuesFromFields(insertFieldsEl, "insert")
    };
  }

  function collectUpdateArgs() {
    const table = ensureText(updateTableSelectEl.value, "更新表名");
    const key = parseInteger(updateKeyInputEl.value, "更新主键 key");
    const values = collectValuesFromFields(updateFieldsEl, "update");
    return {
      table: table,
      key: key,
      values: values
    };
  }

  function syncIndexName(force) {
    const table = (manageTableSelectEl.value || "").trim();
    const suggested = defaultIndexName(table);
    const current = (indexNameInputEl.value || "").trim();

    if (force || !current || indexNameInputEl.dataset.auto === "true") {
      indexNameInputEl.value = suggested;
      indexNameInputEl.dataset.auto = "true";
      return;
    }

    if (current === suggested) {
      indexNameInputEl.dataset.auto = "true";
    }
  }

  function createKVEditor() {
    const root = document.createElement("div");
    root.className = "kv-editor";

    const tableWrap = document.createElement("div");
    tableWrap.className = "table-wrap";

    const table = document.createElement("table");
    table.className = "editor-table";

    const thead = document.createElement("thead");
    thead.innerHTML = "<tr><th>字段名</th><th>类型</th><th>值</th><th>操作</th></tr>";

    const tbody = document.createElement("tbody");

    table.appendChild(thead);
    table.appendChild(tbody);
    tableWrap.appendChild(table);

    const actions = document.createElement("div");
    actions.className = "actions";
    const addBtn = document.createElement("button");
    addBtn.type = "button";
    addBtn.className = "ghost";
    addBtn.textContent = "添加字段";
    actions.appendChild(addBtn);

    root.appendChild(tableWrap);
    root.appendChild(actions);

    function appendRow(initial) {
      const tr = document.createElement("tr");

      const keyTd = document.createElement("td");
      const keyInput = document.createElement("input");
      keyInput.type = "text";
      keyInput.className = "kv-key";
      keyInput.placeholder = "字段名";
      if (initial && typeof initial.key === "string") {
        keyInput.value = initial.key;
      }
      keyTd.appendChild(keyInput);

      const typeTd = document.createElement("td");
      const typeSelect = document.createElement("select");
      typeSelect.className = "kv-type";
      VALUE_TYPE_OPTIONS.forEach(function (typeName) {
        const option = document.createElement("option");
        option.value = typeName;
        option.textContent = typeName;
        typeSelect.appendChild(option);
      });
      if (initial && typeof initial.type === "string") {
        typeSelect.value = initial.type;
      }
      typeTd.appendChild(typeSelect);

      const valueTd = document.createElement("td");
      const valueHost = document.createElement("div");
      valueHost.className = "kv-value-host";
      valueTd.appendChild(valueHost);

      const opTd = document.createElement("td");
      const removeBtn = document.createElement("button");
      removeBtn.type = "button";
      removeBtn.className = "ghost remove-col-btn";
      removeBtn.textContent = "删除";
      opTd.appendChild(removeBtn);

      function renderValueInput() {
        valueHost.innerHTML = "";
        const typeName = typeSelect.value;

        if (typeName === "boolean") {
          const boolSelect = document.createElement("select");
          boolSelect.className = "kv-value";
          const trueOption = document.createElement("option");
          trueOption.value = "true";
          trueOption.textContent = "true";
          const falseOption = document.createElement("option");
          falseOption.value = "false";
          falseOption.textContent = "false";
          boolSelect.appendChild(trueOption);
          boolSelect.appendChild(falseOption);
          if (initial && typeof initial.value === "boolean") {
            boolSelect.value = initial.value ? "true" : "false";
          }
          valueHost.appendChild(boolSelect);
          return;
        }

        const input = document.createElement("input");
        input.className = "kv-value";
        if (typeName === "integer" || typeName === "number") {
          input.type = "number";
          input.step = typeName === "integer" ? "1" : "any";
        } else {
          input.type = "text";
        }
        if (initial && Object.prototype.hasOwnProperty.call(initial, "value") && initial.value !== null && initial.value !== undefined) {
          input.value = String(initial.value);
        }
        valueHost.appendChild(input);
      }

      typeSelect.addEventListener("change", renderValueInput);
      renderValueInput();

      removeBtn.addEventListener("click", function () {
        tr.remove();
        if (tbody.children.length === 0) {
          appendRow();
        }
      });

      tr.appendChild(keyTd);
      tr.appendChild(typeTd);
      tr.appendChild(valueTd);
      tr.appendChild(opTd);
      tbody.appendChild(tr);
    }

    addBtn.addEventListener("click", function () {
      appendRow();
    });

    appendRow();

    return {
      root: root,
      collect: function () {
        const rows = tbody.querySelectorAll("tr");
        if (!rows || rows.length === 0) {
          throw new Error("至少需要 1 个字段值");
        }

        const values = {};
        const seen = {};

        for (let i = 0; i < rows.length; i++) {
          const row = rows[i];
          const keyInput = row.querySelector(".kv-key");
          const typeSelect = row.querySelector(".kv-type");
          const valueInput = row.querySelector(".kv-value");

          const key = ensureText(keyInput.value, "第 " + (i + 1) + " 行字段名");
          if (seen[key]) {
            throw new Error("字段名重复: " + key);
          }
          seen[key] = true;

          const typeName = typeSelect.value;

          if (typeName === "boolean") {
            values[key] = valueInput.value === "true";
            continue;
          }

          const raw = (valueInput.value || "").trim();
          if (typeName === "integer") {
            values[key] = parseInteger(raw, "字段 " + key);
            continue;
          }
          if (typeName === "number") {
            if (!raw) {
              throw new Error("字段 " + key + " 不能为空");
            }
            const number = Number(raw);
            if (!Number.isFinite(number)) {
              throw new Error("字段 " + key + " 必须是数字");
            }
            values[key] = number;
            continue;
          }

          values[key] = valueInput.value || "";
        }

        return values;
      }
    };
  }

  function createActionArgsEditor(actionName) {
    const root = document.createElement("div");
    root.className = "step-args";

    function appendField(labelText, inputEl) {
      const row = document.createElement("div");
      row.className = "row compact-row";
      const label = document.createElement("label");
      label.textContent = labelText;
      row.appendChild(label);
      row.appendChild(inputEl);
      root.appendChild(row);
    }

    function tableInput(defaultText) {
      const input = document.createElement("input");
      input.type = "text";
      input.placeholder = "表名";
      if (defaultText) {
        input.value = defaultText;
      }
      return input;
    }

    if (actionName === "list_tables") {
      const tip = document.createElement("p");
      tip.className = "subtle";
      tip.textContent = "此动作无参数。";
      root.appendChild(tip);
      return {
        root: root,
        collectArgs: async function () {
          return {};
        }
      };
    }

    if (actionName === "table_exists" || actionName === "describe_table" || actionName === "drop_table") {
      const tableEl = tableInput(state.tables[0] || "");
      appendField("表名", tableEl);
      return {
        root: root,
        collectArgs: async function () {
          return { table: ensureText(tableEl.value, "表名") };
        }
      };
    }

    if (actionName === "get_by_primary_int" || actionName === "delete_by_primary_int") {
      const tableEl = tableInput(state.tables[0] || "");
      const keyEl = document.createElement("input");
      keyEl.type = "number";
      keyEl.step = "1";
      keyEl.value = "1";
      appendField("表名", tableEl);
      appendField("主键 key", keyEl);
      return {
        root: root,
        collectArgs: async function () {
          return {
            table: ensureText(tableEl.value, "表名"),
            key: parseInteger(keyEl.value, "主键 key")
          };
        }
      };
    }

    if (actionName === "scan_all") {
      const tableEl = tableInput(state.tables[0] || "");
      const limitEl = document.createElement("input");
      limitEl.type = "number";
      limitEl.step = "1";
      limitEl.min = "1";
      limitEl.value = "20";
      appendField("表名", tableEl);
      appendField("limit", limitEl);
      return {
        root: root,
        collectArgs: async function () {
          return {
            table: ensureText(tableEl.value, "表名"),
            limit: parsePositiveInteger(limitEl.value, "limit")
          };
        }
      };
    }

    if (actionName === "scan_primary_int_range") {
      const tableEl = tableInput(state.tables[0] || "");
      const startEl = document.createElement("input");
      startEl.type = "number";
      startEl.step = "1";
      startEl.value = "1";
      const endEl = document.createElement("input");
      endEl.type = "number";
      endEl.step = "1";
      endEl.value = "100";
      const limitEl = document.createElement("input");
      limitEl.type = "number";
      limitEl.step = "1";
      limitEl.min = "1";
      limitEl.value = "20";

      appendField("表名", tableEl);
      appendField("start_key", startEl);
      appendField("end_key", endEl);
      appendField("limit", limitEl);

      return {
        root: root,
        collectArgs: async function () {
          const startKey = parseInteger(startEl.value, "start_key");
          const endKey = parseInteger(endEl.value, "end_key");
          if (startKey > endKey) {
            throw new Error("start_key 不能大于 end_key");
          }
          return {
            table: ensureText(tableEl.value, "表名"),
            start_key: startKey,
            end_key: endKey,
            limit: parsePositiveInteger(limitEl.value, "limit")
          };
        }
      };
    }

    if (actionName === "insert_row") {
      const tableEl = tableInput(state.tables[0] || "");
      const kvEditor = createKVEditor();
      appendField("表名", tableEl);
      root.appendChild(kvEditor.root);
      return {
        root: root,
        collectArgs: async function () {
          return {
            table: ensureText(tableEl.value, "表名"),
            values: kvEditor.collect()
          };
        }
      };
    }

    if (actionName === "update_by_primary_int") {
      const tableEl = tableInput(state.tables[0] || "");
      const keyEl = document.createElement("input");
      keyEl.type = "number";
      keyEl.step = "1";
      keyEl.value = "1";
      const kvEditor = createKVEditor();

      appendField("表名", tableEl);
      appendField("主键 key", keyEl);
      root.appendChild(kvEditor.root);

      return {
        root: root,
        collectArgs: async function () {
          return {
            table: ensureText(tableEl.value, "表名"),
            key: parseInteger(keyEl.value, "主键 key"),
            values: kvEditor.collect()
          };
        }
      };
    }

    if (actionName === "create_table") {
      const tableEl = document.createElement("input");
      tableEl.type = "text";
      tableEl.placeholder = "例如 users_new";

      appendField("表名", tableEl);

      const wrap = document.createElement("div");
      wrap.className = "row compact-row";
      const label = document.createElement("label");
      label.textContent = "列定义";
      wrap.appendChild(label);

      const tableWrap = document.createElement("div");
      tableWrap.className = "table-wrap";
      const table = document.createElement("table");
      table.className = "editor-table";
      table.innerHTML = "<thead><tr><th>列名</th><th>类型</th><th>长度</th><th>nullable</th><th>操作</th></tr></thead>";
      const tbody = document.createElement("tbody");
      table.appendChild(tbody);
      tableWrap.appendChild(table);
      wrap.appendChild(tableWrap);
      root.appendChild(wrap);

      const actions = document.createElement("div");
      actions.className = "actions";
      const addBtn = document.createElement("button");
      addBtn.type = "button";
      addBtn.className = "ghost";
      addBtn.textContent = "添加列";
      actions.appendChild(addBtn);
      root.appendChild(actions);

      addBtn.addEventListener("click", function () {
        appendColumnRow(tbody);
      });

      appendColumnRow(tbody, { name: "id", type: "INTEGER", nullable: false });
      appendColumnRow(tbody, { name: "name", type: "VARCHAR", nullable: false, length: 64 });

      return {
        root: root,
        collectArgs: async function () {
          return {
            table: ensureText(tableEl.value, "表名"),
            columns: collectColumnsFromBody(tbody, "列定义")
          };
        }
      };
    }

    if (actionName === "create_primary_int_index" || actionName === "drop_index") {
      const tableEl = tableInput(state.tables[0] || "");
      const indexEl = document.createElement("input");
      indexEl.type = "text";
      indexEl.placeholder = "例如 idx_users_id";
      indexEl.value = defaultIndexName(state.tables[0] || "");

      tableEl.addEventListener("input", function () {
        if (!(indexEl.value || "").trim()) {
          indexEl.value = defaultIndexName(tableEl.value);
        }
      });

      appendField("表名", tableEl);
      appendField("索引名", indexEl);
      return {
        root: root,
        collectArgs: async function () {
          return {
            table: ensureText(tableEl.value, "表名"),
            index: ensureText(indexEl.value, "索引名")
          };
        }
      };
    }

    const fallbackTip = document.createElement("p");
    fallbackTip.className = "subtle";
    fallbackTip.textContent = "该动作当前无预置表单，请改用高级调试。";
    root.appendChild(fallbackTip);

    return {
      root: root,
      collectArgs: async function () {
        throw new Error("该动作暂不支持批处理表单，请使用高级调试");
      }
    };
  }

  function renumberBatchSteps() {
    state.batchSteps.forEach(function (step, index) {
      step.titleEl.textContent = "步骤 " + (index + 1);
    });
  }

  function moveBatchStep(step, direction) {
    const index = state.batchSteps.indexOf(step);
    if (index < 0) {
      return;
    }
    const target = index + direction;
    if (target < 0 || target >= state.batchSteps.length) {
      return;
    }

    state.batchSteps.splice(index, 1);
    state.batchSteps.splice(target, 0, step);

    batchStepsEl.innerHTML = "";
    state.batchSteps.forEach(function (item) {
      batchStepsEl.appendChild(item.root);
    });

    renumberBatchSteps();
  }

  function removeBatchStep(step) {
    const index = state.batchSteps.indexOf(step);
    if (index >= 0) {
      state.batchSteps.splice(index, 1);
    }
    step.root.remove();
    renumberBatchSteps();
    renderAvailability();
    renderBatchEmptyState();
  }

  function renderBatchEmptyState() {
    if (state.batchSteps.length > 0) {
      const empty = batchStepsEl.querySelector(".batch-empty");
      if (empty) {
        empty.remove();
      }
      return;
    }
    if (batchStepsEl.querySelector(".batch-empty")) {
      return;
    }
    const empty = document.createElement("div");
    empty.className = "empty-result batch-empty";
    empty.textContent = "尚未添加批处理步骤。";
    batchStepsEl.appendChild(empty);
  }

  function updateBatchStepEditor(step, actionName) {
    step.argsHost.innerHTML = "";
    const editor = createActionArgsEditor(actionName);
    step.collectArgs = editor.collectArgs;
    step.argsHost.appendChild(editor.root);
  }

  function populateBatchStepActionSelect(selectEl, selected) {
    const names = getBatchActionNames();
    selectEl.innerHTML = "";

    if (names.length === 0) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "(无动作可用)";
      selectEl.appendChild(option);
      selectEl.disabled = true;
      return "";
    }

    names.forEach(function (name) {
      const option = document.createElement("option");
      option.value = name;
      option.textContent = name;
      selectEl.appendChild(option);
    });

    selectEl.disabled = false;

    if (selected && names.indexOf(selected) >= 0) {
      selectEl.value = selected;
      return selected;
    }
    return names[0];
  }

  function addBatchStep(initialActionName) {
    const id = state.batchStepCounter;
    state.batchStepCounter += 1;

    const stepRoot = document.createElement("div");
    stepRoot.className = "batch-step";
    stepRoot.dataset.stepId = String(id);

    const head = document.createElement("div");
    head.className = "batch-step-head";

    const title = document.createElement("strong");
    title.className = "batch-step-title";
    title.textContent = "步骤";

    const actionSelect = document.createElement("select");
    actionSelect.className = "batch-step-action";

    const tools = document.createElement("div");
    tools.className = "batch-step-tools";

    const upBtn = document.createElement("button");
    upBtn.type = "button";
    upBtn.className = "ghost";
    upBtn.textContent = "上移";

    const downBtn = document.createElement("button");
    downBtn.type = "button";
    downBtn.className = "ghost";
    downBtn.textContent = "下移";

    const removeBtn = document.createElement("button");
    removeBtn.type = "button";
    removeBtn.className = "ghost";
    removeBtn.textContent = "删除";

    tools.appendChild(upBtn);
    tools.appendChild(downBtn);
    tools.appendChild(removeBtn);

    head.appendChild(title);
    head.appendChild(actionSelect);
    head.appendChild(tools);

    const argsHost = document.createElement("div");
    argsHost.className = "batch-step-args";

    stepRoot.appendChild(head);
    stepRoot.appendChild(argsHost);

    const step = {
      id: id,
      root: stepRoot,
      titleEl: title,
      actionSelectEl: actionSelect,
      argsHost: argsHost,
      collectArgs: async function () {
        return {};
      }
    };

    const picked = populateBatchStepActionSelect(actionSelect, initialActionName || "");
    if (picked) {
      updateBatchStepEditor(step, picked);
    }

    actionSelect.addEventListener("change", function () {
      const actionName = (actionSelect.value || "").trim();
      if (!actionName) {
        return;
      }
      updateBatchStepEditor(step, actionName);
    });

    upBtn.addEventListener("click", function () {
      moveBatchStep(step, -1);
    });

    downBtn.addEventListener("click", function () {
      moveBatchStep(step, 1);
    });

    removeBtn.addEventListener("click", function () {
      removeBatchStep(step);
    });

    const empty = batchStepsEl.querySelector(".batch-empty");
    if (empty) {
      empty.remove();
    }

    state.batchSteps.push(step);
    batchStepsEl.appendChild(stepRoot);
    renumberBatchSteps();
    renderAvailability();
  }

  function refreshBatchActionOptions() {
    const actionNames = getBatchActionNames();

    state.batchSteps.forEach(function (step) {
      const previous = step.actionSelectEl.value;
      const selected = populateBatchStepActionSelect(step.actionSelectEl, previous);

      if (!selected) {
        step.argsHost.innerHTML = "";
        step.collectArgs = async function () {
          throw new Error("当前没有可用批处理动作");
        };
        return;
      }

      if (selected !== previous) {
        updateBatchStepEditor(step, selected);
      }
    });

    if (actionNames.length === 0) {
      state.batchSteps.slice().forEach(function (step) {
        removeBatchStep(step);
      });
    }

    renderBatchEmptyState();
  }

  async function runBatchFromUI() {
    if (state.batchSteps.length === 0) {
      throw new Error("请先添加至少 1 个步骤");
    }

    const mode = (batchModeSelectEl.value || "read_only").trim();
    const stopOnError = !!batchStopOnErrorInputEl.checked;

    const requests = [];

    for (let i = 0; i < state.batchSteps.length; i++) {
      const step = state.batchSteps[i];
      const actionName = ensureText(step.actionSelectEl.value, "步骤 " + (i + 1) + " 动作");
      const args = await step.collectArgs();
      requests.push({ action: actionName, args: args });
    }

    const containsWrite = requests.some(function (req) {
      return WRITE_ACTIONS.has(req.action);
    });

    if (containsWrite && mode !== "read_write") {
      throw new Error("批处理中包含写动作，请将 Mode 设为 read_write");
    }

    return executeAction("batch", {
      stop_on_error: stopOnError,
      requests: requests
    }, mode, "batch");
  }

  function syncDebugActionPreset(forceArgs) {
    const actionName = (actionSelectEl.value || "").trim();
    if (!actionName) {
      return;
    }

    if (WRITE_ACTIONS.has(actionName)) {
      modeSelectEl.value = "read_write";
    }

    const currentArgs = (argsInputEl.value || "").trim();
    if (forceArgs || !currentArgs || currentArgs === "{}") {
      argsInputEl.value = pretty(defaultArgsForAction(actionName));
    }

    try {
      envelopeOutEl.textContent = pretty(buildDebugEnvelope());
    } catch (_err) {
      envelopeOutEl.textContent = "{}";
    }
  }

  function buildDebugEnvelope() {
    const actionName = ensureText(actionSelectEl.value, "action");
    const mode = (modeSelectEl.value || "").trim();

    if (mode !== "read_only" && mode !== "read_write") {
      throw new Error("mode 必须为 read_only 或 read_write");
    }

    let args;
    try {
      args = JSON.parse((argsInputEl.value || "").trim() || "{}");
    } catch (err) {
      throw new Error("args JSON 解析失败: " + err.message);
    }

    if (!args || Array.isArray(args) || typeof args !== "object") {
      throw new Error("args 必须是 JSON 对象");
    }

    return buildEnvelope(actionName, mode, args, "debug");
  }

  async function switchDatabase(path) {
    const candidate = (path || "").trim();
    setBusy(true);
    setStatus("切换数据库中...", false);

    try {
      state.tableSchemaCache = {};
      await loadCapabilities(candidate, true);
      setStatus("数据库切换成功。", false);
    } catch (err) {
      setStatus("切换失败: " + (err && err.message ? err.message : "unknown error"), true);
    } finally {
      setBusy(false);
      renderAvailability();
    }
  }

  function buildQueryAction() {
    const table = ensureText(queryTableSelectEl.value, "查询表名");
    const limit = parsePositiveInteger(queryLimitInputEl.value, "limit");
    const filterMode = queryFilterModeEl.value;

    if (filterMode === "none") {
      return {
        action: "scan_all",
        mode: "read_only",
        args: { table: table, limit: limit }
      };
    }

    if (filterMode === "key_exact") {
      return {
        action: "get_by_primary_int",
        mode: "read_only",
        args: {
          table: table,
          key: parseInteger(queryExactKeyInputEl.value, "主键 key")
        }
      };
    }

    if (filterMode === "key_range") {
      const startKey = parseInteger(queryStartKeyInputEl.value, "start_key");
      const endKey = parseInteger(queryEndKeyInputEl.value, "end_key");
      if (startKey > endKey) {
        throw new Error("start_key 不能大于 end_key");
      }
      return {
        action: "scan_primary_int_range",
        mode: "read_only",
        args: {
          table: table,
          start_key: startKey,
          end_key: endKey,
          limit: limit
        }
      };
    }

    throw new Error("未知筛选模式");
  }

  function wireEvents() {
    switchDbBtn.addEventListener("click", function () {
      fireAndForget(switchDatabase(dbPathInputEl.value));
    });

    useRecentBtn.addEventListener("click", function () {
      const selected = recentDbSelectEl.value || "";
      dbPathInputEl.value = selected;
      fireAndForget(switchDatabase(selected));
    });

    listTablesBtn.addEventListener("click", function () {
      fireAndForget((async function () {
        await executeAction("list_tables", {}, "read_only", "list");
        await loadCapabilities(state.activeDBPath, true);
      })());
    });

    manageTableSelectEl.addEventListener("change", function () {
      syncIndexName(false);
    });

    indexNameInputEl.addEventListener("input", function () {
      const table = (manageTableSelectEl.value || "").trim();
      const suggested = defaultIndexName(table);
      const current = (indexNameInputEl.value || "").trim();
      indexNameInputEl.dataset.auto = current === suggested ? "true" : "false";
    });

    tableExistsBtn.addEventListener("click", function () {
      const table = (manageTableSelectEl.value || "").trim();
      if (!table) {
        setStatus("请先选择表。", true);
        return;
      }
      fireAndForget(executeAction("table_exists", { table: table }, "read_only", "table-exists"));
    });

    describeTableBtn.addEventListener("click", function () {
      const table = (manageTableSelectEl.value || "").trim();
      if (!table) {
        setStatus("请先选择表。", true);
        return;
      }
      fireAndForget(executeAction("describe_table", { table: table }, "read_only", "describe"));
    });

    dropTableBtn.addEventListener("click", function () {
      const table = (manageTableSelectEl.value || "").trim();
      if (!table) {
        setStatus("请先选择要删除的表。", true);
        return;
      }
      if (!window.confirm("确认删除表 " + table + " ? 仅空表允许删除。")) {
        return;
      }
      fireAndForget(executeAction("drop_table", { table: table }, "read_write", "drop-table"));
    });

    createIndexBtn.addEventListener("click", function () {
      const table = (manageTableSelectEl.value || "").trim();
      if (!table) {
        setStatus("请先选择表。", true);
        return;
      }
      const index = (indexNameInputEl.value || "").trim() || defaultIndexName(table);
      fireAndForget(executeAction("create_primary_int_index", { table: table, index: index }, "read_write", "create-index"));
    });

    dropIndexBtn.addEventListener("click", function () {
      const table = (manageTableSelectEl.value || "").trim();
      if (!table) {
        setStatus("请先选择表。", true);
        return;
      }
      const index = (indexNameInputEl.value || "").trim() || defaultIndexName(table);
      if (!window.confirm("确认删除索引 " + index + " ? 仅空表允许删除。")) {
        return;
      }
      fireAndForget(executeAction("drop_index", { table: table, index: index }, "read_write", "drop-index"));
    });

    addCreateColumnBtn.addEventListener("click", function () {
      appendColumnRow(createColumnsBodyEl);
    });

    createTableBtn.addEventListener("click", function () {
      const table = (createTableNameEl.value || "").trim();
      if (!table) {
        setStatus("表名不能为空。", true);
        return;
      }

      let columns;
      try {
        columns = collectColumnsFromBody(createColumnsBodyEl, "列定义");
      } catch (err) {
        setStatus("建表参数错误: " + err.message, true);
        return;
      }

      fireAndForget(executeAction("create_table", { table: table, columns: columns }, "read_write", "create-table"));
    });

    queryFilterModeEl.addEventListener("change", function () {
      renderQueryFilterRows();
    });

    runQueryBtn.addEventListener("click", function () {
      let req;
      try {
        req = buildQueryAction();
      } catch (err) {
        setStatus(err.message, true);
        return;
      }
      fireAndForget(executeAction(req.action, req.args, req.mode, "query"));
    });

    refreshQueryBtn.addEventListener("click", function () {
      let req;
      try {
        req = buildQueryAction();
      } catch (err) {
        setStatus(err.message, true);
        return;
      }
      fireAndForget(executeAction(req.action, req.args, req.mode, "refresh"));
    });

    insertTableSelectEl.addEventListener("change", function () {
      fireAndForget(refreshInsertFields(false));
    });

    updateTableSelectEl.addEventListener("change", function () {
      fireAndForget(refreshUpdateFields(false));
    });

    insertRowBtn.addEventListener("click", function () {
      let args;
      try {
        args = collectInsertValues();
      } catch (err) {
        setStatus("插入参数错误: " + err.message, true);
        return;
      }

      fireAndForget(executeAction("insert_row", args, "read_write", "insert"));
    });

    updateRowBtn.addEventListener("click", function () {
      let args;
      try {
        args = collectUpdateArgs();
      } catch (err) {
        setStatus("更新参数错误: " + err.message, true);
        return;
      }

      fireAndForget(executeAction("update_by_primary_int", args, "read_write", "update"));
    });

    deleteRowBtn.addEventListener("click", function () {
      const table = (updateTableSelectEl.value || "").trim();
      if (!table) {
        setStatus("请先选择表。", true);
        return;
      }

      let key;
      try {
        key = parseInteger(deleteKeyInputEl.value, "删除主键 key");
      } catch (err) {
        setStatus(err.message, true);
        return;
      }

      if (!window.confirm("确认删除 " + table + " 表中主键为 " + key + " 的记录?")) {
        return;
      }

      fireAndForget(executeAction("delete_by_primary_int", { table: table, key: key }, "read_write", "delete"));
    });

    addBatchStepBtn.addEventListener("click", function () {
      addBatchStep();
    });

    runBatchBtn.addEventListener("click", function () {
      fireAndForget((async function () {
        try {
          await runBatchFromUI();
        } catch (err) {
          setStatus("批处理参数错误: " + (err && err.message ? err.message : "unknown error"), true);
        }
      })());
    });

    actionSelectEl.addEventListener("change", function () {
      syncDebugActionPreset(true);
    });

    modeSelectEl.addEventListener("change", function () {
      const actionName = (actionSelectEl.value || "").trim();
      if (WRITE_ACTIONS.has(actionName) && modeSelectEl.value !== "read_write") {
        modeSelectEl.value = "read_write";
      }
      try {
        envelopeOutEl.textContent = pretty(buildDebugEnvelope());
      } catch (_err) {
        envelopeOutEl.textContent = "{}";
      }
    });

    buildEnvelopeBtn.addEventListener("click", function () {
      try {
        const envelope = buildDebugEnvelope();
        envelopeOutEl.textContent = pretty(envelope);
        setStatus("Envelope 已生成。", false);
      } catch (err) {
        setStatus(err.message, true);
      }
    });

    runEnvelopeBtn.addEventListener("click", function () {
      let envelope;
      try {
        envelope = buildDebugEnvelope();
      } catch (err) {
        setStatus(err.message, true);
        return;
      }
      fireAndForget(runEnvelope(envelope));
    });
  }

  async function init() {
    wireEvents();

    appendColumnRow(createColumnsBodyEl, { name: "id", type: "INTEGER", nullable: false });
    appendColumnRow(createColumnsBodyEl, { name: "name", type: "VARCHAR", nullable: false, length: 64 });

    renderQueryFilterRows();
    renderRecentDBPaths();
    updateDBInfo();
    renderValueFields(insertFieldsEl, [], "insert", "");
    renderValueFields(updateFieldsEl, [], "update", "");
    renderBatchEmptyState();

    const stored = localStorage.getItem(STORAGE_ACTIVE_DB_PATH) || "";
    dbPathInputEl.value = stored;

    setBusy(true);
    try {
      await loadCapabilities(stored, true);

      if (state.batchSteps.length === 0 && getBatchActionNames().length > 0) {
        addBatchStep();
      }

      envelopeOutEl.textContent = pretty(buildDebugEnvelope());
      setStatus("系统就绪。", false);
    } catch (err) {
      setStatus("初始化失败: " + (err && err.message ? err.message : "unknown error"), true);
    } finally {
      setBusy(false);
      renderAvailability();
    }
  }

  init();
})();
