"use strict";

// ---------------------------------------------------------------- helpers ----
const $ = (id) => document.getElementById(id);
const pad = (n) => String(n).padStart(2, "0");

function setText(id, v) { const e = $(id); if (e) e.textContent = v; }

// ------------------------------------------------------------------- tabs ----
document.querySelectorAll("nav button").forEach((btn) => {
    btn.addEventListener("click", () => {
        document.querySelectorAll("nav button").forEach((b) => b.classList.remove("active"));
        document.querySelectorAll(".tab").forEach((t) => t.classList.remove("active"));
        btn.classList.add("active");
        $(btn.dataset.tab).classList.add("active");
    });
});

// --------------------------------------------------------- live SBMS data ----
function renderSbms(d) {
    const soc = d.soc ?? 0;
    setText("soc", soc);
    $("socbar").style.width = Math.max(0, Math.min(100, soc)) + "%";

    const t = d.time || {};
    setText("clock", `20${pad(t.year)}-${pad(t.month)}-${pad(t.day)} ` +
                     `${pad(t.hour)}:${pad(t.minute)}:${pad(t.second)}`);

    // cells
    const cells = d.cellsMV || [];
    const lows = d.cellsMinMV || [];
    const highs = d.cellsMaxMV || [];
    const valid = cells.filter((v) => v > 0);
    const lo = Math.min(...valid), hi = Math.max(...valid);
    const body = $("cells").querySelector("tbody");
    body.innerHTML = "";
    const volt = (v) => (v > 0 ? (v / 1000).toFixed(3) + " V" : "--");
    cells.forEach((mv, i) => {
        const tr = document.createElement("tr");
        if (mv > 0 && mv === lo) tr.className = "lo";
        else if (mv > 0 && mv === hi) tr.className = "hi";
        tr.innerHTML = `<td>Cell ${i + 1}</td><td class="now">${volt(mv)}</td>` +
                       `<td>${volt(lows[i])}</td><td>${volt(highs[i])}</td>`;
        body.appendChild(tr);
    });
    setText("celldelta", d.flags && d.flags.delta != null
        ? `Δ ${d.flags.delta} mV` : (valid.length ? `Δ ${hi - lo} mV` : ""));

    // currents (mA -> A)
    const c = d.currentMA || {};
    for (const k of ["battery", "pv1", "pv2", "extLoad"]) {
        setText("c_" + k, c[k] != null ? (c[k] / 1000).toFixed(2) + " A" : "--");
    }

    setText("t_int", d.tempInt != null ? d.tempInt.toFixed(1) + " °C" : "--");
    setText("t_ext", d.tempExt != null ? d.tempExt.toFixed(1) + " °C" : "--");
    setText("ad", `${d.ad2 ?? "-"} / ${d.ad3 ?? "-"} / ${d.ad4 ?? "-"}`);
    setText("heat", `${d.heat1 ?? "-"} / ${d.heat2 ?? "-"}`);

    // flags
    const fl = d.flags || {};
    const box = $("flags");
    box.innerHTML = "";
    Object.keys(fl).filter((k) => k !== "delta").forEach((k) => {
        const s = document.createElement("span");
        s.className = "flag" + (fl[k] ? " on" : "");
        s.textContent = k;
        box.appendChild(s);
    });
}

function connectStream() {
    const es = new EventSource("/eData");
    const mark = (on) => {
        const e = $("conn");
        e.textContent = on ? "live" : "offline";
        e.className = "conn " + (on ? "on" : "off");
    };
    es.onopen = () => mark(true);
    es.onerror = () => mark(false);          // EventSource auto-reconnects
    es.addEventListener("sbms", (ev) => {
        try { renderSbms(JSON.parse(ev.data)); mark(true); }
        catch (e) { console.error("bad sbms payload", e); }
    });
}

// --------------------------------------------------------------- settings ----
const CFG = {
    wifi: {
        title: "WiFi",
        fields: [
            ["hostname", "text", "Hostname"],
            ["sta_ssid", "text", "Station SSID"],
            ["sta_pw", "password", "Station password"],
            ["sta_enable", "bool", "Enable station mode"],
            ["ap_ssid", "text", "AP SSID"],
            ["ap_pw", "password", "AP password"],
        ],
    },
    mqtt: {
        title: "MQTT",
        fields: [
            ["mq_enabled", "bool", "Enable MQTT"],
            ["mq_host", "text", "Broker host"],
            ["mq_port", "number", "Broker port"],
            ["mq_prefix", "text", "Topic prefix"],
            ["mq_user", "text", "Username"],
            ["mq_password", "password", "Password"],
        ],
    },
    data: {
        title: "Data",
        fields: [
            ["sbms_enabled", "bool", "Publish sbms"],
            ["sbms_diff", "bool", "Add cell delta"],
            ["s2_enabled", "bool", "Publish s2"],
        ],
    },
    sys: {
        title: "System",
        fields: [
            ["ota_limit", "bool", "Limit OTA to first 5 min after boot"],
        ],
    },
    email: {
        title: "Status e-mail",
        fields: [
            ["em_enabled", "bool", "Enable daily e-mail"],
            ["em_hour", "number", "Send at hour (0-23)"],
            ["em_host", "text", "SMTP host"],
            ["em_port", "number", "SMTP port"],
            ["em_user", "text", "SMTP username"],
            ["em_password", "password", "SMTP password"],
            ["em_from", "text", "From address"],
            ["em_to", "text", "To address"],
            ["em_ssl", "bool", "Use SSL/TLS"],
            ["em_tls_verify", "bool", "Verify server certificate"],
        ],
    },
};

function buildCfgForm(name, spec) {
    const form = document.createElement("form");
    form.className = "cfg";
    form.innerHTML = `<h2>${spec.title}</h2>`;

    const inputs = {};
    for (const [key, type, label] of spec.fields) {
        const l = document.createElement("label");
        const span = document.createElement("span");
        span.textContent = label;
        const inp = document.createElement("input");
        inp.type = type === "bool" ? "checkbox" : type;
        if (type === "number") { inp.min = 0; inp.step = 1; }
        inputs[key] = [inp, type];
        l.append(span, inp);
        form.appendChild(l);
    }

    const bar = document.createElement("div");
    const save = document.createElement("button");
    save.type = "submit";
    save.textContent = "Save";
    const status = document.createElement("span");
    status.className = "saved";
    bar.append(save, status);
    form.appendChild(bar);

    const populate = (cfg) => {
        for (const [key, [inp, type]] of Object.entries(inputs)) {
            const v = cfg[key];
            if (type === "bool") inp.checked = !!v;
            else inp.value = v == null ? "" : v;
        }
    };

    fetch("/cfg/" + name)
        .then((r) => (r.ok ? r.json() : {}))
        .then(populate)
        .catch(() => {});

    form.addEventListener("submit", (e) => {
        e.preventDefault();
        const out = {};
        for (const [key, [inp, type]] of Object.entries(inputs)) {
            if (type === "bool") out[key] = inp.checked;
            else if (type === "number") out[key] = Number(inp.value || 0);
            else out[key] = inp.value;
        }
        status.textContent = "saving…";
        status.className = "saved";
        fetch("/cfg/" + name, { method: "POST", body: JSON.stringify(out) })
            .then((r) => {
                if (!r.ok) throw new Error(r.status);
                status.textContent = "saved";
                status.className = "saved ok";
            })
            .catch((err) => {
                status.textContent = "failed (" + err.message + ")";
                status.className = "saved err";
            });
    });

    return form;
}

function buildSettings() {
    const host = $("cfgforms");
    for (const [name, spec] of Object.entries(CFG)) {
        host.appendChild(buildCfgForm(name, spec));
    }
}

// ------------------------------------------------------------------ debug ----
function loadInto(id, url) {
    setText(id, "loading…");
    fetch(url).then((r) => r.text()).then((t) => setText(id, t || "(empty)"))
        .catch((e) => setText(id, "error: " + e.message));
}

const LOAD_MAP = {
    debug: ["taskdump", "/debug"],
    raw: ["rawdump", "/rawData"],
    email: ["emailstatus", "/email/status"],
};
document.querySelectorAll("[data-load]").forEach((btn) => {
    btn.addEventListener("click", () => loadInto(...LOAD_MAP[btn.dataset.load]));
});

$("emailtest").addEventListener("click", () => {
    setText("emailstatus", "sending test…");
    fetch("/email/test", { method: "POST" })
        .then((r) => r.text())
        .then(() => {
            let i = 0;
            const poll = setInterval(() => {
                loadInto("emailstatus", "/email/status");
                if (++i >= 8) clearInterval(poll);
            }, 1500);
        })
        .catch((e) => setText("emailstatus", "error: " + e.message));
});

$("otago").addEventListener("click", () => {
    const f = $("otafile").files[0];
    const st = $("otastatus");
    if (!f) { st.textContent = "pick a file first"; return; }
    const fd = new FormData();
    fd.append("update", f, f.name);
    st.textContent = "uploading " + f.name + "…";
    fetch("/update", { method: "POST", body: fd })
        .then((r) => r.text())
        .then((t) => { st.textContent = t.trim() + " — device will reboot if it succeeded"; })
        .catch((e) => { st.textContent = "error: " + e.message; });
});

$("cellreset").addEventListener("click", () => {
    const st = $("cellresetstatus");
    st.textContent = "resetting…";
    st.className = "saved";
    fetch("/cells/reset", { method: "POST" })
        .then((r) => {
            if (!r.ok) throw new Error(r.status);
            st.textContent = "done";
            st.className = "saved ok";
        })
        .catch((err) => {
            st.textContent = "failed (" + err.message + ")";
            st.className = "saved err";
        });
});

$("reboot").addEventListener("click", () => {
    if (!confirm("Reboot the device now?")) return;
    $("rebootstatus").textContent = "rebooting…";
    fetch("/reboot", { method: "POST" }).catch(() => {});
});

// -------------------------------------------------------------------- init ----
fetch("/version").then((r) => r.text()).then((v) => setText("version", v.trim()))
    .catch(() => setText("version", "unknown"));
buildSettings();
connectStream();
loadInto("taskdump", "/debug");
loadInto("rawdump", "/rawData");
loadInto("emailstatus", "/email/status");
