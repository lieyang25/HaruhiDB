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
  const describeTableBtn = document.getElementById("describeTableBtn");
  const dropTableBtn = document.getElementById("dropTableBtn");

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
    lastQuery: null
  };

  const busyButtons = [
    switchDbBtn,
    useRecentBtn,
    listTablesBtn,
    describeTableBtn,
    dropTableBtn,
    addCreateColumnBtn,
    createTableBtn,
    runQueryBtn,
    refreshQueryBtn,
    insertRowBtn,
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

  function pretty(value) {
    try {
      return JSON.stringify(value, null, 2);
    } catch (e) {
      return String(value);
    }
  }

  async function requestJSON(url, options) {
    const response = await fetch(url, options);

    let parsed;
    try {
      parsed = await response.json();
    } catch (e) {
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
    } catch (e) {
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

  function renderTableSelect(selectEl) {
    const previous = selectEl.value;
    selectEl.innerHTML = "";

    if (!state.tables || state.tables.length === 0) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "(无表)";
      selectEl.appendChild(option);
      selectEl.disabled = true;
      return;
    }

    state.tables.forEach(function (table) {
      const option = document.createElement("option");
      option.value = table;
      option.textContent = table;
      selectEl.appendChild(option);
    });

    if (previous && state.tables.indexOf(previous) >= 0) {
      selectEl.value = previous;
    }

    selectEl.disabled = false;
  }

  function renderAllTableSelectors() {
    [manageTableSelectEl, queryTableSelectEl, insertTableSelectEl].forEach(function (selectEl) {
      renderTableSelect(selectEl);
    });
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

  function renderAvailability() {
    const hasTables = state.tables && state.tables.length > 0;
    listTablesBtn.disabled = !actionSupported("list_tables", "read_only");
    describeTableBtn.disabled = !hasTables || !actionSupported("describe_table", "read_only");
    dropTableBtn.disabled = !hasTables || !actionSupported("drop_table", "read_write");
    createTableBtn.disabled = !actionSupported("create_table", "read_write");
    runQueryBtn.disabled = !hasTables;
    refreshQueryBtn.disabled = !hasTables;
    insertRowBtn.disabled = !hasTables || !actionSupported("insert_row", "read_write");
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
        return { table: table, index: "idx_" + table + "_id" };
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

    const actions = state.capabilities && Array.isArray(state.capabilities.actions)
      ? state.capabilities.actions
      : [];

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

  function renderSimpleMessage(text, kind) {
    const div = document.createElement("div");
    div.className = kind === "error" ? "result-error" : "result-success";
    div.textContent = text;
    return div;
  }

  function summarizeSuccess(actionName, data) {
    if (data && typeof data === "object") {
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
      resultViewEl.appendChild(renderSimpleMessage("服务返回了非 JSON 响应。", "error"));
      return;
    }

    if (!response.ok) {
      const code = response.error && response.error.code ? response.error.code : "UNKNOWN";
      const message = response.error && response.error.message ? response.error.message : "unknown error";
      resultSummaryEl.textContent = "执行失败。";
      resultViewEl.appendChild(renderSimpleMessage("[" + code + "] " + message, "error"));
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
        const err = item.error && item.error.message ? item.error.message : "";
        return {
          index: item.index,
          action: item.action,
          ok: item.ok,
          message: err
        };
      });
      resultViewEl.appendChild(makeTableFromRows(rows));
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
    renderAvailability();
    updateDBInfo();

    if (state.tables.length > 0) {
      await refreshInsertFields(true);
    } else {
      renderInsertFields([], "");
    }

    if (!keepStatusText) {
      setStatus("能力已加载，当前数据库已就绪。", false);
    }
  }

  function addCreateColumnRow(initial) {
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
      typeSelect.value = initial.type;
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
      if (createColumnsBodyEl.children.length === 0) {
        addCreateColumnRow();
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
    createColumnsBodyEl.appendChild(tr);
  }

  function collectCreateColumns() {
    const rows = createColumnsBodyEl.querySelectorAll("tr");
    if (!rows || rows.length === 0) {
      throw new Error("至少需要定义 1 列");
    }

    const columns = [];
    const seen = {};

    for (let i = 0; i < rows.length; i++) {
      const row = rows[i];
      const name = (row.querySelector(".create-col-name").value || "").trim();
      const type = row.querySelector(".create-col-type").value;
      const nullable = !!row.querySelector(".create-col-nullable").checked;
      const lengthText = (row.querySelector(".create-col-length").value || "").trim();

      if (!name) {
        throw new Error("第 " + (i + 1) + " 行列名不能为空");
      }
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
        const length = parsePositiveInteger(lengthText, "第 " + (i + 1) + " 行 VARCHAR 长度");
        column.length = length;
      }

      columns.push(column);
    }

    return columns;
  }

  function renderInsertFields(columns, table) {
    insertFieldsEl.innerHTML = "";

    if (!table) {
      const empty = document.createElement("div");
      empty.className = "empty-result";
      empty.textContent = "请选择表后再插入。";
      insertFieldsEl.appendChild(empty);
      return;
    }

    if (!Array.isArray(columns) || columns.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-result";
      empty.textContent = "未获取到列结构，请先点击“查看结构”确认。";
      insertFieldsEl.appendChild(empty);
      return;
    }

    columns.forEach(function (column) {
      const wrapper = document.createElement("div");
      wrapper.className = "insert-field";
      wrapper.dataset.columnName = column.name;
      wrapper.dataset.columnType = String(column.type || "").toUpperCase();

      const label = document.createElement("label");
      label.textContent = column.name + " (" + wrapper.dataset.columnType + ")";
      wrapper.appendChild(label);

      let input;
      if (wrapper.dataset.columnType === "BOOLEAN") {
        const boolWrap = document.createElement("label");
        boolWrap.className = "insert-boolean";
        input = document.createElement("input");
        input.type = "checkbox";
        input.className = "insert-input";
        boolWrap.appendChild(input);
        boolWrap.appendChild(document.createTextNode("true / false"));
        wrapper.appendChild(boolWrap);
      } else {
        input = document.createElement("input");
        input.className = "insert-input";

        if (INTEGER_TYPES.has(wrapper.dataset.columnType)) {
          input.type = "number";
          input.step = "1";
        } else if (NUMBER_TYPES.has(wrapper.dataset.columnType)) {
          input.type = "number";
          input.step = "any";
        } else {
          input.type = "text";
          if (wrapper.dataset.columnType === "VARCHAR" && typeof column.length === "number" && column.length > 0) {
            input.maxLength = column.length;
            input.placeholder = "最长 " + column.length + " 字符";
          }
        }

        wrapper.appendChild(input);
      }

      insertFieldsEl.appendChild(wrapper);
    });
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
      renderInsertFields([], "");
      return;
    }

    try {
      const schema = await loadTableSchema(table);
      renderInsertFields(schema.columns || [], table);
    } catch (err) {
      renderInsertFields([], table);
      if (!silentOnError) {
        setStatus("读取插入表单结构失败: " + (err && err.message ? err.message : "unknown error"), true);
      }
    }
  }

  function collectInsertValues() {
    const table = (insertTableSelectEl.value || "").trim();
    if (!table) {
      throw new Error("请先选择插入目标表");
    }

    const wrappers = insertFieldsEl.querySelectorAll(".insert-field");
    if (!wrappers || wrappers.length === 0) {
      throw new Error("当前表没有可插入列");
    }

    const values = {};

    wrappers.forEach(function (wrapper) {
      const name = wrapper.dataset.columnName;
      const type = wrapper.dataset.columnType;
      const input = wrapper.querySelector(".insert-input");

      if (!name || !input) {
        return;
      }

      if (type === "BOOLEAN") {
        values[name] = !!input.checked;
        return;
      }

      const raw = (input.value || "").trim();
      if (INTEGER_TYPES.has(type)) {
        values[name] = parseInteger(raw, "列 " + name);
        return;
      }

      if (NUMBER_TYPES.has(type)) {
        if (!raw) {
          throw new Error("列 " + name + " 不能为空");
        }
        const number = Number(raw);
        if (!Number.isFinite(number)) {
          throw new Error("列 " + name + " 必须是数字");
        }
        values[name] = number;
        return;
      }

      values[name] = input.value || "";
    });

    return values;
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
    } catch (err) {
      envelopeOutEl.textContent = "{}";
    }
  }

  function buildDebugEnvelope() {
    const actionName = (actionSelectEl.value || "").trim();
    if (!actionName) {
      throw new Error("action 不能为空");
    }

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
    const table = (queryTableSelectEl.value || "").trim();
    if (!table) {
      throw new Error("请选择查询表");
    }

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
      switchDatabase(dbPathInputEl.value);
    });

    useRecentBtn.addEventListener("click", function () {
      const selected = recentDbSelectEl.value || "";
      dbPathInputEl.value = selected;
      switchDatabase(selected);
    });

    listTablesBtn.addEventListener("click", async function () {
      try {
        await executeAction("list_tables", {}, "read_only", "list");
        await loadCapabilities(state.activeDBPath, true);
      } catch (_err) {
        return;
      }
    });

    describeTableBtn.addEventListener("click", function () {
      const table = (manageTableSelectEl.value || "").trim();
      if (!table) {
        setStatus("请先选择表。", true);
        return;
      }
      executeAction("describe_table", { table: table }, "read_only", "describe");
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
      executeAction("drop_table", { table: table }, "read_write", "drop-table");
    });

    addCreateColumnBtn.addEventListener("click", function () {
      addCreateColumnRow();
    });

    createTableBtn.addEventListener("click", function () {
      const table = (createTableNameEl.value || "").trim();
      if (!table) {
        setStatus("表名不能为空。", true);
        return;
      }

      let columns;
      try {
        columns = collectCreateColumns();
      } catch (err) {
        setStatus("建表参数错误: " + err.message, true);
        return;
      }

      executeAction("create_table", { table: table, columns: columns }, "read_write", "create-table");
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
      state.lastQuery = req;
      executeAction(req.action, req.args, req.mode, "query");
    });

    refreshQueryBtn.addEventListener("click", function () {
      let req;
      try {
        req = buildQueryAction();
      } catch (err) {
        setStatus(err.message, true);
        return;
      }
      state.lastQuery = req;
      executeAction(req.action, req.args, req.mode, "refresh");
    });

    insertTableSelectEl.addEventListener("change", function () {
      refreshInsertFields(false);
    });

    insertRowBtn.addEventListener("click", function () {
      const table = (insertTableSelectEl.value || "").trim();
      if (!table) {
        setStatus("请先选择插入目标表。", true);
        return;
      }

      let values;
      try {
        values = collectInsertValues();
      } catch (err) {
        setStatus("插入参数错误: " + err.message, true);
        return;
      }

      executeAction("insert_row", { table: table, values: values }, "read_write", "insert");
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
      runEnvelope(envelope);
    });
  }

  async function init() {
    wireEvents();

    addCreateColumnRow({ name: "id", type: "INTEGER", nullable: false });
    addCreateColumnRow({ name: "name", type: "VARCHAR", nullable: false, length: 64 });

    renderQueryFilterRows();
    renderRecentDBPaths();
    updateDBInfo();
    renderInsertFields([], "");

    const stored = localStorage.getItem(STORAGE_ACTIVE_DB_PATH) || "";
    dbPathInputEl.value = stored;

    setBusy(true);
    try {
      await loadCapabilities(stored, true);
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
