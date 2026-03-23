(function () {
  const strictTemplate = "严格只输出与目标直接相关的动作，不允许任何额外动作。请按顺序仅执行 4 步：1) insert_row(table=student, values={id:303,name:'instruct_test'})；2) get_by_primary_int(table=student,key=303)；3) delete_by_primary_int(table=student,key=303)；4) get_by_primary_int(table=student,key=303)。禁止 list_tables/table_exists/describe_table/scan_all/scan_primary_int_range/update_by_primary_int。";

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

  function setBusy(busy) {
    translateBtn.disabled = busy;
    runBtn.disabled = busy;
    strictBtn.disabled = busy;
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

  async function postJSON(url, payload) {
    const resp = await fetch(url, {
      method: "POST",
      headers: buildHeaders(),
      body: JSON.stringify(payload)
    });

    let parsed;
    try {
      parsed = await resp.json();
    } catch (e) {
      parsed = { error: { message: "response is not valid JSON" } };
    }

    if (!resp.ok) {
      const message = parsed && parsed.error && parsed.error.message ? parsed.error.message : "request failed";
      const err = new Error(message);
      err.payload = parsed;
      throw err;
    }

    return parsed;
  }

  async function run(execute) {
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
        request_id: "ui-" + Date.now(),
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

      const executed = await postJSON("/v1/action", translated.candidate_envelope);
      executionOutEl.textContent = pretty(executed);
      setStatus("执行完成。", false);
    } catch (err) {
      if (err && err.payload) {
        executionOutEl.textContent = pretty(err.payload);
      }
      setStatus("失败: " + (err && err.message ? err.message : "unknown error"), true);
    } finally {
      setBusy(false);
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

  strictBtn.addEventListener("click", function () {
    inputEl.value = strictTemplate;
    setStatus("已填入严格四步示例。", false);
  });

  translateBtn.addEventListener("click", function () {
    run(false);
  });

  runBtn.addEventListener("click", function () {
    run(true);
  });

  copyTranslationBtn.addEventListener("click", function () {
    copyFrom(translationOutEl);
  });

  copyExecutionBtn.addEventListener("click", function () {
    copyFrom(executionOutEl);
  });
})();
