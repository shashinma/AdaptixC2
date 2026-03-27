/// BloodHound CE — reverse proxy teamserver (/service/webproxy/bloodhoundce/): клиент шлёт JWT Adaptix; прокси подставляет upstream_bearer (токен CE) к upstream, если задан в service_config.
/// Панель появляется после первого push с непустым url (нужны upstream_url и public base на сервере).

const BH_PANEL_ID = "bloodhoundce";

var bh_url = "";
var bh_title = "BloodHound CE";
var bh_proxy_host = "";
var bh_proxy_port = 0;
var bh_icon = "";

function bh_merge_config(obj) {
    if (!obj || typeof obj !== "object")
        return;
    if (obj.url !== undefined && obj.url !== null && String(obj.url).length > 0)
        bh_url = String(obj.url);
    if (obj.title !== undefined && obj.title !== null && String(obj.title).length > 0)
        bh_title = String(obj.title);
    if (obj.proxy_host !== undefined && obj.proxy_host !== null)
        bh_proxy_host = String(obj.proxy_host);
    if (obj.proxy_port !== undefined && obj.proxy_port !== null) {
        let p = parseInt(obj.proxy_port, 10);
        bh_proxy_port = isNaN(p) ? 0 : p;
    }
    if (obj.icon !== undefined && obj.icon !== null && String(obj.icon).length > 0)
        bh_icon = String(obj.icon);
}

function bh_apply_modules() {
    if (typeof ax.apply_chromeless_web_modules !== "function")
        return;
    if (bh_url.length === 0)
        return;
    let app = {
        id: BH_PANEL_ID,
        title: bh_title,
        url: bh_url,
        attach_teamserver_bearer: true
    };
    if (bh_proxy_host.length > 0)
        app.proxy_host = bh_proxy_host;
    if (bh_proxy_port > 0)
        app.proxy_port = bh_proxy_port;
    if (bh_icon.length > 0)
        app.icon = bh_icon;
    ax.apply_chromeless_web_modules(JSON.stringify({ apps: [app] }));
}

function bh_open_panel() {
    if (bh_url.length === 0 || typeof ax.open_web_panel !== "function")
        return;
    ax.open_web_panel(true, BH_PANEL_ID, bh_title, bh_url, bh_proxy_host, bh_proxy_port, bh_icon, true);
}

function InitService() {
    if (bh_url.length > 0)
        bh_apply_modules();
    let open_bh = menu.create_action("BloodHound CE", function() { bh_open_panel(); });
    menu.add_main(open_bh);
}

function data_handler(data) {
    if (data === undefined || data === null)
        return;
    let s = String(data);
    if (s.length === 0)
        return;
    let obj;
    try {
        obj = JSON.parse(s);
    } catch (e) {
        return;
    }
    bh_merge_config(obj);
    bh_apply_modules();
}
