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

  const COLUMN_TYPES = ["BOOLEAN", "TINYINT", "SMALLINT", "INTEGER", "BIGINT", "FLOAT", "DOUBLE", "VARCHAR"];

  const statusEl = document.getElementById("status");
  const envelopeOutEl = document.getElementById("envelopeOut");
  const executionOutEl = document.getElementById("executionOut");

  const dbPathInputEl = document.getElementById("dbPathInput");
  const switchDbBtn = document.getElementById("switchDbBtn");
  const recentDbSelectEl = document.getElementById("recentDbSelect");
  const useRecentBtn = document.getElementById("useRecentBtn");
  const dbInfoEl = document.getElementById("dbInfo");
  const capabilitiesInfoEl = document.getElementById("capabilitiesInfo");

  const qaListTablesBtn = document.getElementById("qaListTablesBtn");
  const qaDescribeTableEl = document.getElementById("qaDescribeTable");
  const qaDescribeBtn = document.getElementById("qaDescribeBtn");
  const qaDropTableEl = document.getElementById("qaDropTable");
  const qaDropBtn = document.getElementById("qaDropBtn");
  const qaCreateTableNameEl = document.getElementById("qaCreateTableName");
  const qaColumnsEl = document.getElementById("qaColumns");
  const qaAddColumnBtn = document.getElementById("qaAddColumnBtn");
  const qaCreateBtn = document.getElementById("qaCreateBtn");

  const actionSelectEl = document.getElementById("actionSelect");
  const modeSelectEl = document.getElementById("modeSelect");
  const argsInputEl = document.getElementById("argsInput");
  const buildEnvelopeBtn = document.getElementById("buildEnvelopeBtn");
  const runEnvelopeBtn = document.getElementById("runEnvelopeBtn");
  const copyEnvelopeBtn = document.getElementById("copyEnvelopeBtn");
  const copyExecutionBtn = document.getElementById("copyExecutionBtn");

  const state = {
    activeDBPath: "",
    defaultDBPath: "",
    tables: [],
    capabilities: null
  };

  const busyButtons = [
    switchDbBtn,
    useRecentBtn,
    qaListTablesBtn,
    qaDescribeBtn,
    qaDropBtn,
    qaAddColumnBtn,
    qaCreateBtn,
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
      dbInfoEl.textContent = "当前数据库: (未连接)";
      return;
    }

    const defaultLabel = state.defaultDBPath ? " | 默认库: " + state.defaultDBPath : "";
    dbInfoEl.textContent = "当前数据库: " + state.activeDBPath + defaultLabel;
  }

  function renderTableSelect(selectEl) {
    selectEl.innerHTML = "";
    if (!state.tables || state.tables.length === 0) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "(无表)";
      selectEl.appendChild(option);
      return;
    }

    state.tables.forEach(function (table) {
      const option = document.createElement("option");
      option.value = table;
      option.textContent = table;
      selectEl.appendChild(option);
    });
  }

  function renderTableSelectors() {
    renderTableSelect(qaDescribeTableEl);
    renderTableSelect(qaDropTableEl);
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

  function modeForAction(actionName) {
    if (WRITE_ACTIONS.has(actionName)) {
      return "read_write";
    }
    return modeSelectEl.value;
  }

  function renderQuickActionAvailability() {
    const canListTables = actionSupported("list_tables", modeForAction("list_tables"));
    const canDescribeTable = actionSupported("describe_table", modeForAction("describe_table"));
    const canCreateTable = actionSupported("create_table", "read_write");
    const canDropTable = actionSupported("drop_table", "read_write");
    const hasTables = state.tables && state.tables.length > 0;

    qaListTablesBtn.disabled = !canListTables;
    qaDescribeBtn.disabled = !canDescribeTable || !hasTables;
    qaCreateBtn.disabled = !canCreateTable;
    qaDropBtn.disabled = !canDropTable || !hasTables;

    qaDescribeTableEl.disabled = !hasTables;
    qaDropTableEl.disabled = !hasTables;
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
      " | read_only 动作: " + roCount +
      " | read_write 动作: " + rwCount +
      " | 当前表数量: " + state.tables.length;
  }

  function buildCapabilitiesURL(dbPath) {
    const trimmed = (dbPath || "").trim();
    if (!trimmed) {
      return "/v1/capabilities";
    }
    return "/v1/capabilities?db_path=" + encodeURIComponent(trimmed);
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

  function renderActionOptions() {
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

    syncActionPreset(true);
  }

  function syncActionPreset(forceArgs) {
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
      const envelope = buildEnvelope();
      envelopeOutEl.textContent = pretty(envelope);
    } catch (err) {
      envelopeOutEl.textContent = "{}";
    }
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
    renderTableSelectors();
    renderCapabilitiesSummary();
    renderQuickActionAvailability();
    renderActionOptions();
    updateDBInfo();

    if (!keepStatusText) {
      setStatus("能力已加载，当前数据库已就绪。", false);
    }
  }

  function addColumnRow(initial) {
    const row = document.createElement("div");
    row.className = "column-row";

    const nameInput = document.createElement("input");
    nameInput.type = "text";
    nameInput.className = "col-name";
    nameInput.placeholder = "列名";

    const typeSelect = document.createElement("select");
    typeSelect.className = "col-type";
    COLUMN_TYPES.forEach(function (typeName) {
      const option = document.createElement("option");
      option.value = typeName;
      option.textContent = typeName;
      typeSelect.appendChild(option);
    });

    const nullableWrap = document.createElement("label");
    nullableWrap.className = "nullable-wrap";
    const nullableInput = document.createElement("input");
    nullableInput.type = "checkbox";
    nullableInput.className = "col-nullable";
    nullableWrap.appendChild(nullableInput);
    nullableWrap.appendChild(document.createTextNode(" nullable"));

    const lengthInput = document.createElement("input");
    lengthInput.type = "number";
    lengthInput.className = "col-length";
    lengthInput.min = "1";
    lengthInput.step = "1";
    lengthInput.placeholder = "varchar 长度";

    const removeBtn = document.createElement("button");
    removeBtn.type = "button";
    removeBtn.className = "ghost small remove";
    removeBtn.textContent = "删除";

    function syncLengthAvailability() {
      const isVarchar = typeSelect.value === "VARCHAR";
      lengthInput.disabled = !isVarchar;
      if (!isVarchar) {
        lengthInput.value = "";
      }
    }

    typeSelect.addEventListener("change", syncLengthAvailability);
    removeBtn.addEventListener("click", function () {
      row.remove();
      if (qaColumnsEl.children.length === 0) {
        addColumnRow();
      }
    });

    if (initial && typeof initial.name === "string") {
      nameInput.value = initial.name;
    }
    if (initial && typeof initial.type === "string") {
      typeSelect.value = initial.type;
    }
    if (initial && typeof initial.nullable === "boolean") {
      nullableInput.checked = initial.nullable;
    }
    if (initial && typeof initial.length === "number" && initial.length > 0) {
      lengthInput.value = String(initial.length);
    }

    row.appendChild(nameInput);
    row.appendChild(typeSelect);
    row.appendChild(nullableWrap);
    row.appendChild(lengthInput);
    row.appendChild(removeBtn);

    qaColumnsEl.appendChild(row);
    syncLengthAvailability();
  }

  function collectCreateColumns() {
    const rows = qaColumnsEl.querySelectorAll(".column-row");
    if (!rows || rows.length === 0) {
      throw new Error("至少需要定义 1 列");
    }

    const columns = [];
    const seenNames = {};

    for (let i = 0; i < rows.length; i++) {
      const row = rows[i];
      const nameInput = row.querySelector(".col-name");
      const typeSelect = row.querySelector(".col-type");
      const nullableInput = row.querySelector(".col-nullable");
      const lengthInput = row.querySelector(".col-length");

      const name = (nameInput && nameInput.value ? nameInput.value : "").trim();
      if (!name) {
        throw new Error("第 " + (i + 1) + " 列的列名不能为空");
      }
      if (seenNames[name]) {
        throw new Error("列名重复: " + name);
      }
      seenNames[name] = true;

      const typeName = typeSelect ? typeSelect.value : "";
      const nullable = !!(nullableInput && nullableInput.checked);

      const column = {
        name: name,
        type: typeName,
        nullable: nullable
      };

      if (typeName === "VARCHAR") {
        const rawLength = (lengthInput && lengthInput.value ? lengthInput.value : "").trim();
        const parsedLength = Number(rawLength);
        if (!rawLength || !Number.isInteger(parsedLength) || parsedLength <= 0) {
          throw new Error("第 " + (i + 1) + " 列的 VARCHAR 长度必须为正整数");
        }
        column.length = parsedLength;
      }

      columns.push(column);
    }

    return columns;
  }

  async function refreshCapabilitiesAfterWrite() {
    try {
      await loadCapabilities(state.activeDBPath, true);
    } catch (err) {
      setStatus("写操作成功，但刷新能力失败: " + err.message, true);
    }
  }

  function buildEnvelope() {
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

    return {
      version: "v3",
      request_id: newRequestID("ui"),
      mode: mode,
      action: actionName,
      args: args
    };
  }

  async function executeEnvelope() {
    let envelope;
    try {
      envelope = buildEnvelope();
    } catch (err) {
      setStatus(err.message, true);
      return;
    }

    envelopeOutEl.textContent = pretty(envelope);

    const payload = Object.assign({}, envelope);
    if (state.activeDBPath) {
      payload.db_path = state.activeDBPath;
    }

    setBusy(true);
    setStatus("执行中...", false);

    try {
      const result = await postJSON("/v1/action", payload);
      executionOutEl.textContent = pretty(result);
      setStatus("执行完成。", false);

      if (result && result.ok && (WRITE_ACTIONS.has(envelope.action) || envelope.action === "batch")) {
        await refreshCapabilitiesAfterWrite();
      }
    } catch (err) {
      if (err && err.payload) {
        executionOutEl.textContent = pretty(err.payload);
      }
      setStatus("失败: " + (err && err.message ? err.message : "unknown error"), true);
    } finally {
      setBusy(false);
      renderQuickActionAvailability();
    }
  }

  async function executeAction(actionName, args, forcedMode) {
    const envelope = {
      version: "v3",
      request_id: newRequestID("qa"),
      mode: forcedMode || modeForAction(actionName),
      action: actionName,
      args: args || {}
    };

    envelopeOutEl.textContent = pretty(envelope);

    const payload = Object.assign({}, envelope);
    if (state.activeDBPath) {
      payload.db_path = state.activeDBPath;
    }

    setBusy(true);
    setStatus("执行中...", false);

    try {
      const result = await postJSON("/v1/action", payload);
      executionOutEl.textContent = pretty(result);
      setStatus("执行完成。", false);

      if (result && result.ok && (WRITE_ACTIONS.has(actionName) || actionName === "batch")) {
        await refreshCapabilitiesAfterWrite();
      }
    } catch (err) {
      if (err && err.payload) {
        executionOutEl.textContent = pretty(err.payload);
      }
      setStatus("失败: " + (err && err.message ? err.message : "unknown error"), true);
    } finally {
      setBusy(false);
      renderQuickActionAvailability();
    }
  }

  async function copyFrom(targetEl) {
    const text = targetEl.textContent || "";
    try {
      await navigator.clipboard.writeText(text);
      setStatus("已复制到剪贴板。", false);
    } catch (e) {
      setStatus("复制失败，请手动复制。", true);
    }
  }

  async function switchDatabase(path) {
    const candidate = (path || "").trim();
    setBusy(true);
    setStatus("切换数据库中...", false);

    try {
      await loadCapabilities(candidate, true);
      setStatus("数据库切换成功。", false);
    } catch (err) {
      setStatus("切换失败: " + (err && err.message ? err.message : "unknown error"), true);
    } finally {
      setBusy(false);
      renderQuickActionAvailability();
    }
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

    qaListTablesBtn.addEventListener("click", function () {
      executeAction("list_tables", {});
    });

    qaDescribeBtn.addEventListener("click", function () {
      const table = (qaDescribeTableEl.value || "").trim();
      if (!table) {
        setStatus("请先选择表。", true);
        return;
      }
      executeAction("describe_table", { table: table });
    });

    qaDropBtn.addEventListener("click", function () {
      const table = (qaDropTableEl.value || "").trim();
      if (!table) {
        setStatus("请先选择要删除的表。", true);
        return;
      }
      if (!window.confirm("确认删除表 " + table + " ? 此操作不可撤销。")) {
        return;
      }
      executeAction("drop_table", { table: table }, "read_write");
    });

    qaAddColumnBtn.addEventListener("click", function () {
      addColumnRow();
    });

    qaCreateBtn.addEventListener("click", function () {
      const table = (qaCreateTableNameEl.value || "").trim();
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

      executeAction("create_table", {
        table: table,
        columns: columns
      }, "read_write");
    });

    actionSelectEl.addEventListener("change", function () {
      syncActionPreset(true);
      renderQuickActionAvailability();
    });

    modeSelectEl.addEventListener("change", function () {
      const actionName = (actionSelectEl.value || "").trim();
      if (WRITE_ACTIONS.has(actionName) && modeSelectEl.value !== "read_write") {
        modeSelectEl.value = "read_write";
      }
      try {
        envelopeOutEl.textContent = pretty(buildEnvelope());
      } catch (err) {
        envelopeOutEl.textContent = "{}";
      }
      renderQuickActionAvailability();
    });

    buildEnvelopeBtn.addEventListener("click", function () {
      try {
        const envelope = buildEnvelope();
        envelopeOutEl.textContent = pretty(envelope);
        setStatus("Envelope 已生成。", false);
      } catch (err) {
        setStatus(err.message, true);
      }
    });

    runEnvelopeBtn.addEventListener("click", function () {
      executeEnvelope();
    });

    copyEnvelopeBtn.addEventListener("click", function () {
      copyFrom(envelopeOutEl);
    });

    copyExecutionBtn.addEventListener("click", function () {
      copyFrom(executionOutEl);
    });
  }

  async function init() {
    wireEvents();

    addColumnRow({ name: "id", type: "INTEGER", nullable: false });
    addColumnRow({ name: "name", type: "VARCHAR", nullable: false, length: 64 });

    renderRecentDBPaths();
    updateDBInfo();

    const stored = localStorage.getItem(STORAGE_ACTIVE_DB_PATH) || "";
    dbPathInputEl.value = stored;

    setBusy(true);
    try {
      await loadCapabilities(stored, true);
      envelopeOutEl.textContent = pretty(buildEnvelope());
      setStatus("系统就绪。", false);
    } catch (err) {
      setStatus("初始化失败: " + (err && err.message ? err.message : "unknown error"), true);
    } finally {
      setBusy(false);
      renderQuickActionAvailability();
    }
  }

  init();
})();
