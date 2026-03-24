(function () {
  const strictTemplate = "请完成这个四步流程（可直接用 batch）：1) insert_row(table=student, values={id:303,name:'instruct_test'})；2) get_by_primary_int(table=student,key=303)；3) delete_by_primary_int(table=student,key=303)；4) get_by_primary_int(table=student,key=303)。除非必要，不要添加无关动作。";
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

  const modeEl = document.getElementById("mode");
  const tokenEl = document.getElementById("authToken");
  const inputEl = document.getElementById("nlInput");
  const statusEl = document.getElementById("status");
  const translationOutEl = document.getElementById("translationOut");
  const executionOutEl = document.getElementById("executionOut");
  const translateBtn = document.getElementById("translateBtn");
  const runBtn = document.getElementById("runBtn");
  const strictBtn = document.getElementById("strictBtn");
  const copyTranslationBtn = document.getElementById("copyTranslationBtn");
  const copyExecutionBtn = document.getElementById("copyExecutionBtn");

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

  const state = {
    activeDBPath: "",
    defaultDBPath: "",
    tables: [],
    capabilities: null
  };

  const busyButtons = [
    translateBtn,
    runBtn,
    strictBtn,
    switchDbBtn,
    useRecentBtn,
    qaListTablesBtn,
    qaDescribeBtn,
    qaDropBtn,
    qaAddColumnBtn,
    qaCreateBtn
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

  function buildHeaders() {
    const headers = {
      "Content-Type": "application/json"
    };
    const token = tokenEl.value.trim();
    if (token) {
      headers.Authorization = "Bearer " + token;
    }
    return headers;
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
      headers: buildHeaders(),
      body: JSON.stringify(payload)
    });
  }

  async function getJSON(url) {
    return requestJSON(url, {
      method: "GET",
      headers: buildHeaders()
    });
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
    return modeEl.value;
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

  async function executeAction(actionName, args, forcedMode) {
    const envelope = {
      version: "v3",
      request_id: newRequestID("qa"),
      mode: forcedMode || modeForAction(actionName),
      action: actionName,
      args: args || {}
    };

    const payload = Object.assign({}, envelope);
    if (state.activeDBPath) {
      payload.db_path = state.activeDBPath;
    }

    setBusy(true);
    setStatus("执行中...", false);
    translationOutEl.textContent = pretty(envelope);

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

  function candidateContainsWriteAction(candidate) {
    if (!candidate || typeof candidate !== "object") {
      return false;
    }

    const actionName = typeof candidate.action === "string" ? candidate.action.trim() : "";
    if (WRITE_ACTIONS.has(actionName)) {
      return true;
    }
    if (actionName !== "batch") {
      return false;
    }

    const args = candidate.args;
    if (!args || typeof args !== "object" || !Array.isArray(args.requests)) {
      return false;
    }

    return args.requests.some(function (item) {
      return item && typeof item.action === "string" && WRITE_ACTIONS.has(item.action.trim());
    });
  }

  async function runNL(execute) {
    const natural = inputEl.value.trim();
    if (!natural) {
      setStatus("请输入自然语言请求。", true);
      return;
    }

    setBusy(true);
    setStatus(execute ? "翻译并执行中..." : "翻译中...", false);
    translationOutEl.textContent = "{}";
    executionOutEl.textContent = "{}";

    try {
      const request = {
        request_id: newRequestID("ui"),
        db_path: state.activeDBPath,
        input: natural,
        mode: modeEl.value
      };

      const translated = await postJSON("/v1/nl/translate", request);
      translationOutEl.textContent = pretty(translated);

      if (!execute) {
        setStatus("翻译完成。", false);
        return;
      }

      if (!translated.valid || !translated.candidate_envelope) {
        setStatus("翻译结果不可执行，请先调整输入。", true);
        return;
      }

      const actionPayload = Object.assign({}, translated.candidate_envelope);
      if (state.activeDBPath) {
        actionPayload.db_path = state.activeDBPath;
      }

      const executed = await postJSON("/v1/action", actionPayload);
      executionOutEl.textContent = pretty(executed);
      setStatus("执行完成。", false);

      if (executed && executed.ok && candidateContainsWriteAction(translated.candidate_envelope)) {
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
    strictBtn.addEventListener("click", function () {
      inputEl.value = strictTemplate;
      setStatus("已填入四步流程示例。", false);
    });

    translateBtn.addEventListener("click", function () {
      runNL(false);
    });

    runBtn.addEventListener("click", function () {
      runNL(true);
    });

    copyTranslationBtn.addEventListener("click", function () {
      copyFrom(translationOutEl);
    });

    copyExecutionBtn.addEventListener("click", function () {
      copyFrom(executionOutEl);
    });

    switchDbBtn.addEventListener("click", function () {
      switchDatabase(dbPathInputEl.value);
    });

    useRecentBtn.addEventListener("click", function () {
      const selected = recentDbSelectEl.value || "";
      dbPathInputEl.value = selected;
      switchDatabase(selected);
    });

    modeEl.addEventListener("change", function () {
      renderQuickActionAvailability();
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
