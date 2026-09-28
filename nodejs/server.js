// Node.js QmlServer. Same requests, pages and options as the Qt server in ../server.
// The Qt executable is unchanged; run this with: node server.js -help
//
// The raw `net` module is used on purpose. Node's `http` module would answer
// HEAD, pipelined requests and the access-log columns differently from
// HttpServer.cpp, and those details are what the browser was checked against.
// Reads are synchronous so two requests already in the buffer stay in order.

"use strict";

const fs = require("fs");
const net = require("net");
const path = require("path");

const VERSION = "1.0";
const MAX_HEADER = 16 * 1024;

const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

const CONTENT_TYPES = {
    qml: "text/x-qml; charset=utf-8",
    js: "text/javascript; charset=utf-8",
    mjs: "text/javascript; charset=utf-8",
    json: "application/json; charset=utf-8",
    txt: "text/plain; charset=utf-8",
    html: "text/html; charset=utf-8",
    css: "text/css; charset=utf-8",
    svg: "image/svg+xml",
    png: "image/png",
    jpg: "image/jpeg",
    jpeg: "image/jpeg",
    gif: "image/gif",
    webp: "image/webp",
    ico: "image/vnd.microsoft.icon",
    ttf: "font/ttf",
    otf: "font/otf",
};

const REASONS = {
    200: "OK",
    400: "Bad Request",
    403: "Forbidden",
    404: "Not Found",
    405: "Method Not Allowed",
    414: "URI Too Long",
    500: "Internal Server Error",
};

function reasonPhrase(code) {
    return REASONS[code] || "Unknown";
}

function contentTypeFor(suffix) {
    return CONTENT_TYPES[String(suffix || "").toLowerCase()] || "text/plain; charset=utf-8";
}

function qmlEscape(text) {
    return String(text).replace(/\\/g, "\\\\").replace(/"/g, "\\\"");
}

function qmlPage(title, heading, accent, body) {
    return [
        "import QtQuick",
        "",
        "Rectangle {",
        `    property string title: "${qmlEscape(title)}"`,
        "    color: \"#11131a\"",
        "    Column {",
        "        anchors.centerIn: parent",
        "        width: Math.min(parent.width - 96, 720)",
        "        spacing: 18",
        "        Text {",
        `            text: "${qmlEscape(heading)}"`,
        `            color: "${accent}"`,
        "            font.pixelSize: 44",
        "            font.bold: true",
        "        }",
        "        Text {",
        "            width: parent.width",
        `            text: "${qmlEscape(body)}"`,
        "            color: \"#aeb6c6\"",
        "            font.pixelSize: 16",
        "            wrapMode: Text.WordWrap",
        "        }",
        "        Text {",
        "            text: \"Open the start page\"",
        "            color: home.containsMouse ? \"#b9d0ff\" : \"#8ab4ff\"",
        "            font.pixelSize: 16",
        "            font.underline: home.containsMouse",
        "            MouseArea {",
        "                id: home",
        "                anchors.fill: parent",
        "                hoverEnabled: true",
        "                cursorShape: Qt.PointingHandCursor",
        "                onClicked: browser.navigate(\"/\")",
        "            }",
        "        }",
        "    }",
        "}",
        "",
    ].join("\n");
}

function httpDate(date) {
    const dd = String(date.getUTCDate()).padStart(2, "0");
    const mon = MONTHS[date.getUTCMonth()];
    const hh = String(date.getUTCHours()).padStart(2, "0");
    const mm = String(date.getUTCMinutes()).padStart(2, "0");
    const ss = String(date.getUTCSeconds()).padStart(2, "0");
    return `${dd} ${mon} ${date.getUTCFullYear()} ${hh}:${mm}:${ss} +0000`;
}

function logStamp(date) {
    const hh = String(date.getHours()).padStart(2, "0");
    const mm = String(date.getMinutes()).padStart(2, "0");
    const ss = String(date.getSeconds()).padStart(2, "0");
    return `${hh}:${mm}:${ss}`;
}

function padEnd(text, width) {
    const value = String(text);
    return value.length >= width ? value : value + " ".repeat(width - value.length);
}

function padStart(text, width) {
    const value = String(text);
    return value.length >= width ? value : " ".repeat(width - value.length) + value;
}

function findDocumentRoot() {
    if (process.env.QMLSERVER_ROOT)
        return path.resolve(process.env.QMLSERVER_ROOT);

    let dir = __dirname;
    if (fs.existsSync(path.join(dir, "wwwroot")))
        return path.resolve(dir, "wwwroot");

    for (let i = 0; i < 6; ++i) {
        if (fs.existsSync(path.join(dir, "server", "wwwroot")))
            return path.resolve(dir, "server", "wwwroot");
        const parent = path.dirname(dir);
        if (parent === dir)
            break;
        dir = parent;
    }

    return path.resolve(process.cwd(), "wwwroot");
}

function helpText() {
    return [
        "Usage: node server.js [options]",
        "Serves .qml documents over HTTP for QmlBrowser.",
        "Same behavior as the Qt QmlServer. The Qt executable is left as it is.",
        "",
        "One dash or two both work: -port 9000 and --port 9000 are the same.",
        "With no arguments the server listens on http://127.0.0.1:8080/ and",
        "serves server/wwwroot from the source tree.",
        "",
        "Examples:",
        "  node server.js",
        "  node server.js -port 9000",
        "  node server.js -root D:\\my\\qml\\site -address 0.0.0.0",
        "  node server.js -public",
        "",
        "Options:",
        "  -h, -help, --help       Show this help and exit.",
        "  -v, -version, --version Show the version and exit.",
        "  -p, -port <port>        Port to listen on, from 1 to 65535. Default: 8080.",
        "  -r, -root <directory>   Folder of .qml files. Default: server/wwwroot, or QMLSERVER_ROOT.",
        "  -a, -address <ip>       Listen address. Default: 127.0.0.1. Use 0.0.0.0 for every interface.",
        "  -public                 Listen on every interface. Same as -address 0.0.0.0.",
        "",
    ].join("\n");
}

function fail(message, code) {
    process.stderr.write(message + "\n\n" + helpText());
    process.exit(code);
}

function parseArgs(argv) {
    const options = { help: false, version: false, port: "8080", root: null, address: null, public: false };
    const set = { port: false, address: false };

    for (let i = 0; i < argv.length; ++i) {
        let arg = argv[i];
        if (arg === "--")
            break;

        let inline = null;
        const eq = arg.indexOf("=");
        if (eq > 1 && arg.startsWith("-")) {
            inline = arg.slice(eq + 1);
            arg = arg.slice(0, eq);
        }

        const take = (name) => {
            if (inline !== null)
                return inline;
            if (i + 1 >= argv.length)
                fail(`Missing value after '${arg}'.`, 1);
            return argv[++i];
        };

        // "-public" is one long option. Treating a single dash as a bundle of
        // short flags would read it as -p with the value "ublic".
        const long = arg.startsWith("--") ? arg.slice(2)
            : (arg.startsWith("-") && arg.length > 2 ? arg.slice(1) : null);
        const short = long === null && arg.startsWith("-") && arg.length === 2 ? arg[1] : null;

        if (arg === "-?" || short === "h" || short === "?" || long === "help" || long === "help-all") {
            options.help = true;
        } else if (short === "v" || long === "version") {
            options.version = true;
        } else if (short === "p" || long === "port") {
            options.port = take("port");
            set.port = true;
        } else if (short === "r" || long === "root") {
            options.root = take("root");
            set.root = true;
        } else if (short === "a" || long === "address") {
            options.address = take("address");
            set.address = true;
        } else if (long === "public") {
            options.public = true;
        } else if (arg.startsWith("-")) {
            fail(`Unknown option '${long || arg.slice(1)}'.`, 1);
        } else {
            fail(`Unexpected argument '${arg}'.`, 1);
        }
    }

    options.portSet = set.port;
    options.addressSet = set.address;
    return options;
}

function send(socket, request, code, contentType, body) {
    const payload = Buffer.isBuffer(body) ? body : Buffer.from(body);
    const head = [
        `HTTP/1.1 ${code} ${reasonPhrase(code)}`,
        // "(Node)" is the one intentional difference from the Qt server, so a
        // response shows which process answered. The browser does not read it.
        `Server: QmlServer/1.0 (Node)`,
        `Date: ${httpDate(new Date())}`,
        `Content-Type: ${contentType}`,
        `Content-Length: ${payload.length}`,
        "Cache-Control: no-cache, no-store, must-revalidate",
        `Connection: ${request.keepAlive ? "keep-alive" : "close"}`,
        "",
        "",
    ].join("\r\n");

    socket.write(head);
    if (request.method !== "HEAD")
        socket.write(payload);

    const target = request.target ? request.target : "-";
    process.stdout.write(
        `${logStamp(new Date())}  ${padEnd(request.method, 5)} ${code}  ${padStart(payload.length, 7)} B  ${target}\n`);
}

function sendError(socket, request, code, detail) {
    const heading = `${code} ${reasonPhrase(code)}`;
    send(socket, request, code, "text/x-qml; charset=utf-8", qmlPage(heading, heading, "#ff6b81", detail));
}

function serveFile(socket, request, info) {
    let body;
    try {
        body = fs.readFileSync(info.absolute);
    } catch (error) {
        sendError(socket, request, 500, `Cannot read ${info.name}`);
        return;
    }
    send(socket, request, 200, contentTypeFor(info.suffix), body);
}

function serveDirectory(socket, request, urlPath, dirPath) {
    const index = path.join(dirPath, "index.qml");
    if (fs.existsSync(index) && fs.statSync(index).isFile()) {
        serveFile(socket, request, {
            absolute: index,
            name: "index.qml",
            suffix: "qml",
        });
        return;
    }

    const base = urlPath.endsWith("/") ? urlPath : urlPath + "/";
    let names;
    try {
        names = fs.readdirSync(dirPath);
    } catch (error) {
        sendError(socket, request, 500, `Cannot read ${path.basename(dirPath)}`);
        return;
    }

    const entries = [];
    for (const name of names) {
        let stat;
        try {
            stat = fs.statSync(path.join(dirPath, name));
        } catch (error) {
            continue;
        }
        entries.push({ name, dir: stat.isDirectory(), size: stat.size });
    }
    entries.sort((a, b) => {
        if (a.dir !== b.dir)
            return a.dir ? -1 : 1;
        if (a.name < b.name)
            return -1;
        if (a.name > b.name)
            return 1;
        return 0;
    });

    let listing = "import QtQuick\n\n"
        + "Rectangle {\n"
        + `    property string title: "Index of ${qmlEscape(base)}"\n`
        + "    color: \"#11131a\"\n"
        + "    Flickable {\n"
        + "        anchors.fill: parent\n"
        + "        anchors.margins: 40\n"
        + "        contentHeight: column.height\n"
        + "        clip: true\n"
        + "        Column {\n"
        + "            id: column\n"
        + "            width: parent.width\n"
        + "            spacing: 10\n"
        + "            Text {\n"
        + `                text: "Index of ${qmlEscape(base)}"\n`
        + "                color: \"#f2f5ff\"\n"
        + "                font.pixelSize: 28\n"
        + "                font.bold: true\n"
        + "                bottomPadding: 12\n"
        + "            }\n";

    if (base !== "/") {
        listing += "            Text {\n"
            + "                text: \"../\"\n"
            + "                color: up.containsMouse ? \"#8ab4ff\" : \"#7d8ba6\"\n"
            + "                font.pixelSize: 16\n"
            + "                font.family: \"Segoe UI\"\n"
            + "                MouseArea {\n"
            + "                    id: up\n"
            + "                    anchors.fill: parent\n"
            + "                    hoverEnabled: true\n"
            + "                    cursorShape: Qt.PointingHandCursor\n"
            + "                    onClicked: browser.navigate(\"..\")\n"
            + "                }\n"
            + "            }\n";
    }

    let n = 0;
    for (const entry of entries) {
        const name = entry.dir ? entry.name + "/" : entry.name;
        const size = entry.dir ? "dir" : `${entry.size} B`;
        const link = `link${n++}`;
        listing += "            Row {\n"
            + "                spacing: 14\n"
            + "                Text {\n"
            + `                    text: "${qmlEscape(name)}"\n`
            + `                    color: ${link}.containsMouse ? "#8ab4ff" : "#d7deee"\n`
            + "                    font.pixelSize: 16\n"
            + "                    font.family: \"Segoe UI\"\n"
            + `                    font.underline: ${link}.containsMouse\n`
            + "                    MouseArea {\n"
            + `                        id: ${link}\n`
            + "                        anchors.fill: parent\n"
            + "                        hoverEnabled: true\n"
            + "                        cursorShape: Qt.PointingHandCursor\n"
            + `                        onClicked: browser.navigate("${qmlEscape(name)}")\n`
            + "                    }\n"
            + "                }\n"
            + "                Text {\n"
            + `                    text: "${size}"\n`
            + "                    color: \"#6b7487\"\n"
            + "                    font.pixelSize: 14\n"
            + "                    font.family: \"Segoe UI\"\n"
            + "                }\n"
            + "            }\n";
    }

    listing += "        }\n    }\n}\n";
    send(socket, request, 200, "text/x-qml; charset=utf-8", listing);
}

function dispatch(socket, request, root) {
    if (request.method !== "GET" && request.method !== "HEAD") {
        sendError(socket, request, 405, "Only GET and HEAD are supported.");
        return false;
    }

    let urlPath = request.target;
    const query = urlPath.indexOf("?");
    if (query >= 0)
        urlPath = urlPath.slice(0, query);
    try {
        // Qt's decoder leaves a broken "%" sequence in place. This one rejects
        // it. Paths both sides decode the same way still match status and body.
        urlPath = decodeURIComponent(urlPath);
    } catch (error) {
        sendError(socket, request, 400, "Malformed request target.");
        return false;
    }

    if (!urlPath.startsWith("/")) {
        sendError(socket, request, 400, "Malformed request target.");
        return false;
    }

    for (const segment of urlPath.split("/")) {
        if (segment === "..") {
            sendError(socket, request, 403, "Path traversal is not allowed.");
            return true;
        }
    }

    const relative = urlPath.slice(1);
    const absolute = path.resolve(root, relative);
    const fromRoot = path.relative(root, absolute);
    if (fromRoot.startsWith("..") || path.isAbsolute(fromRoot)) {
        sendError(socket, request, 403, "Outside of the document root.");
        return true;
    }

    let stat;
    try {
        stat = fs.statSync(absolute);
    } catch (error) {
        if (error.code === "ENOENT") {
            sendError(socket, request, 404, `The server has no document at ${urlPath}`);
            return true;
        }
        sendError(socket, request, 500, `Cannot read ${path.basename(absolute)}`);
        return true;
    }

    if (stat.isDirectory()) {
        serveDirectory(socket, request, urlPath, absolute);
        return true;
    }

    serveFile(socket, request, {
        absolute,
        name: path.basename(absolute),
        suffix: path.extname(absolute).slice(1),
    });
    return true;
}

function onData(socket, state, root, chunk) {
    state.buffer = Buffer.concat([state.buffer, chunk]);

    for (;;) {
        const headerEnd = state.buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (state.buffer.length > MAX_HEADER) {
                sendError(socket, { method: "GET", target: "", keepAlive: false }, 414, "");
                socket.end();
            }
            return;
        }

        const head = state.buffer.slice(0, headerEnd).toString("latin1");
        state.buffer = state.buffer.slice(headerEnd + 4);

        const lines = head.split("\n");
        const parts = lines[0].trim().split(" ");
        if (parts.length < 3) {
            sendError(socket, { method: "GET", target: "", keepAlive: false }, 400, "");
            socket.end();
            return;
        }

        const request = {
            method: parts[0].toUpperCase(),
            target: Buffer.from(parts[1], "latin1").toString("utf8"),
            keepAlive: !parts[2].trim().endsWith("1.0"),
        };

        for (let i = 1; i < lines.length; ++i) {
            const line = lines[i].trim();
            if (line.toLowerCase().startsWith("connection:")) {
                const value = line.slice(11).trim().toLowerCase();
                request.keepAlive = value !== "close";
            }
        }

        if (!dispatch(socket, request, root) || !request.keepAlive) {
            socket.end();
            return;
        }
    }
}

function listen(host, port, root) {
    const server = net.createServer((socket) => {
        socket.setNoDelay(true);
        const state = { buffer: Buffer.alloc(0) };
        socket.on("data", (chunk) => onData(socket, state, root, chunk));
        socket.on("error", () => socket.destroy());
    });

    server.on("error", (error) => {
        process.stderr.write(`Cannot listen on ${host}:${port}: ${error.message}\n`);
        process.exit(1);
    });

    server.listen(port, host, () => {
        const shown = path.normalize(root);
        process.stdout.write(
            "QmlServer\n"
            + `  documents   ${shown}\n`
            + `  address     http://${host}:${port}/\n`
            + `  home page   http://${host}:${port}/index.qml\n`
            + "\n"
            + "  -help       show the options\n"
            + "  Ctrl+C      stop the server\n"
            + "\n");
    });
}

function main() {
    const options = parseArgs(process.argv.slice(2));
    if (options.help) {
        process.stdout.write(helpText());
        process.exit(0);
    }
    if (options.version) {
        process.stdout.write(`QmlServer ${VERSION}\n`);
        process.exit(0);
    }

    const portValue = Number(options.port);
    if (!/^\d+$/.test(options.port) || portValue === 0 || portValue > 65535) {
        fail(`Port must be a number from 1 to 65535, not "${options.port}".`, 2);
    }

    if (options.public && options.addressSet)
        fail("Use either -address or -public, not both.", 2);

    let host = "127.0.0.1";
    if (options.public)
        host = "0.0.0.0";
    else if (options.addressSet)
        host = options.address.trim();

    if (host === "0.0.0.0" || host === "*")
        host = "0.0.0.0";
    else if (host.toLowerCase() === "localhost" || host === "127.0.0.1")
        host = "127.0.0.1";
    else if (net.isIP(host) === 0)
        fail(`Address must be an IP address such as 127.0.0.1 or 0.0.0.0, not "${host}".`, 2);

    const rootPath = options.root ? path.resolve(options.root) : findDocumentRoot();
    if (!fs.existsSync(rootPath) || !fs.statSync(rootPath).isDirectory()) {
        fail(`No .qml folder at ${rootPath}.\n`
            + "Pass -root with the directory that contains the pages.", 2);
    }

    listen(host, portValue, rootPath);
}

main();
