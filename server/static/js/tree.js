(() => {
    const refreshIntervalMs = 5000;
    const canvas = document.getElementById("network-graph");
    const dataElement = document.getElementById("tree-graph-data");
    if (!canvas || !dataElement) return;

    const context = canvas.getContext("2d");
    const details = document.getElementById("graph-details");
    const search = document.getElementById("graph-search");
    const resetButton = document.getElementById("graph-reset");
    const colors = {
        server: "#ff5c35",
        nodeOnline: "#22c55e",
        nodeOffline: "#64748b",
        sensor: "#a78bfa",
    };

    let graph = JSON.parse(dataElement.textContent);
    let byId = new Map();
    let width = 0;
    let height = 0;
    let scale = 1;
    let offsetX = 0;
    let offsetY = 0;
    let hovered = null;
    let selected = null;
    let dragging = null;
    let panning = false;
    let moved = false;
    let pointerX = 0;
    let pointerY = 0;
    let refreshing = false;

    const nodeColor = node => {
        if (node.kind === "server") return colors.server;
        if (node.kind === "sensor") return colors.sensor;
        return node.connected ? colors.nodeOnline : colors.nodeOffline;
    };

    const parentLinkFor = node => graph.links.find(link => link.target === node.id);

    const childrenOf = node => graph.links
        .filter(link => link.source === node.id)
        .map(link => byId.get(link.target))
        .filter(Boolean);

    const hydrateGraph = nextGraph => {
        const previous = new Map(graph.nodes.map(node => [node.id, node]));
        graph = nextGraph;
        byId = new Map(graph.nodes.map(node => [node.id, node]));

        const nodeItems = graph.nodes.filter(node => node.kind === "node");
        const sensorItems = graph.nodes.filter(node => node.kind === "sensor");

        graph.nodes.forEach(node => {
            const oldNode = previous.get(node.id);
            if (oldNode) {
                node.x = oldNode.x;
                node.y = oldNode.y;
                node.radius = oldNode.radius;
            }

            if (node.kind === "server") {
                node.x = node.x ?? 0;
                node.y = node.y ?? 0;
                node.radius = 34;
                return;
            }

            if (node.kind === "node") {
                if (node.x === undefined || node.y === undefined) {
                    const position = nodeItems.indexOf(node);
                    const angle = (position / Math.max(nodeItems.length, 1)) *
                        Math.PI * 2 - Math.PI / 2;
                    const radius = 230 + (position % 2) * 45;
                    node.x = Math.cos(angle) * radius;
                    node.y = Math.sin(angle) * radius;
                }
                node.radius = 23;
                return;
            }

            const parentLink = parentLinkFor(node);
            const parent = parentLink ? byId.get(parentLink.source) : null;
            if (!parent) return;

            if (node.x === undefined || node.y === undefined) {
                const siblings = sensorItems.filter(item => {
                    const link = parentLinkFor(item);
                    return link && link.source === parent.id;
                });
                const position = siblings.indexOf(node);
                const baseAngle = Math.atan2(parent.y, parent.x);
                const spread = Math.min(Math.PI * 0.9, siblings.length * 0.35);
                const angle = baseAngle - spread / 2 +
                    ((position + 0.5) / siblings.length) * spread;
                node.x = parent.x + Math.cos(angle) * 105;
                node.y = parent.y + Math.sin(angle) * 105;
            }
            node.radius = 13;
        });

        if (selected) selected = byId.get(selected.id) || null;
        if (hovered) hovered = byId.get(hovered.id) || null;
    };

    function resize() {
        const box = canvas.getBoundingClientRect();
        const pixelRatio = window.devicePixelRatio || 1;
        width = box.width;
        height = box.height;
        canvas.width = Math.round(width * pixelRatio);
        canvas.height = Math.round(height * pixelRatio);
        context.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
        draw();
    }

    function resetView() {
        scale = Math.min(1.1, Math.max(0.55, Math.min(width, height) / 760));
        offsetX = width / 2;
        offsetY = height / 2;
        draw();
    }

    function screenPosition(node) {
        return {
            x: node.x * scale + offsetX,
            y: node.y * scale + offsetY,
        };
    }

    function graphPosition(x, y) {
        return {
            x: (x - offsetX) / scale,
            y: (y - offsetY) / scale,
        };
    }

    function nodeAt(x, y) {
        return [...graph.nodes].reverse().find(node => {
            const point = screenPosition(node);
            return Math.hypot(x - point.x, y - point.y) <= node.radius * scale + 7;
        }) || null;
    }

    function roundedLabel(text, x, y, active) {
        context.font = "600 12px Inter, system-ui, sans-serif";
        const textWidth = context.measureText(text).width;
        const boxWidth = textWidth + 18;
        context.fillStyle = active ? "rgba(255,92,53,.2)" : "rgba(8,11,16,.82)";
        context.strokeStyle = active ? "rgba(255,92,53,.7)" : "rgba(154,167,184,.18)";
        context.lineWidth = 1;
        context.beginPath();
        context.roundRect(x - boxWidth / 2, y, boxWidth, 25, 8);
        context.fill();
        context.stroke();
        context.fillStyle = "#f8fafc";
        context.textAlign = "center";
        context.textBaseline = "middle";
        context.fillText(text, x, y + 12.5);
    }

    function draw() {
        context.clearRect(0, 0, width, height);

        const gradient = context.createRadialGradient(
            offsetX, offsetY, 0, offsetX, offsetY, Math.max(width, height) * 0.7
        );
        gradient.addColorStop(0, "rgba(255,92,53,.07)");
        gradient.addColorStop(1, "rgba(8,11,16,0)");
        context.fillStyle = gradient;
        context.fillRect(0, 0, width, height);

        graph.links.forEach(link => {
            const sourceNode = byId.get(link.source);
            const targetNode = byId.get(link.target);
            if (!sourceNode || !targetNode) return;

            const source = screenPosition(sourceNode);
            const target = screenPosition(targetNode);
            const connected = selected &&
                (selected.id === link.source || selected.id === link.target);
            context.beginPath();
            context.moveTo(source.x, source.y);
            context.lineTo(target.x, target.y);
            context.strokeStyle = connected ?
                "rgba(255,92,53,.85)" :
                "rgba(154,167,184,.22)";
            context.lineWidth = connected ? 2.2 : 1;
            context.stroke();
        });

        const searchTerm = search.value.trim().toLowerCase();
        graph.nodes.forEach(node => {
            const point = screenPosition(node);
            const matches = searchTerm && (
                node.label.toLowerCase().includes(searchTerm) ||
                node.subtitle.toLowerCase().includes(searchTerm)
            );
            const active = node === hovered || node === selected || matches;
            const radius = node.radius * scale;
            const color = nodeColor(node);

            if (active) {
                context.beginPath();
                context.arc(point.x, point.y, radius + 8, 0, Math.PI * 2);
                context.fillStyle = `${color}22`;
                context.fill();
            }

            context.beginPath();
            context.arc(point.x, point.y, radius, 0, Math.PI * 2);
            context.fillStyle = color;
            context.shadowColor = color;
            context.shadowBlur = active ? 22 : 9;
            context.fill();
            context.shadowBlur = 0;

            context.beginPath();
            context.arc(point.x, point.y, Math.max(3, radius * 0.35), 0, Math.PI * 2);
            context.fillStyle = "rgba(255,255,255,.72)";
            context.fill();

            if (node.kind !== "sensor" || active || scale > 0.8) {
                roundedLabel(node.label, point.x, point.y + radius + 8, active);
            }
        });
    }

    function showDetails(node) {
        if (!node) return;
        details.replaceChildren();
        const kind = document.createElement("span");
        const title = document.createElement("strong");
        const subtitle = document.createElement("p");
        kind.className = "details-kind";
        kind.textContent = node.kind === "node" ?
            (node.connected ? "connected node" : "offline node") :
            node.kind;
        title.textContent = node.label;
        subtitle.textContent = node.subtitle;
        details.append(kind, title, subtitle);
        if (node.details) {
            const extra = document.createElement("small");
            extra.textContent = node.details;
            details.append(extra);
        }
    }

    const moveNodeWithSensors = (node, dx, dy) => {
        node.x += dx;
        node.y += dy;
        if (node.kind !== "node") return;

        childrenOf(node)
            .filter(child => child.kind === "sensor")
            .forEach(sensor => {
                sensor.x += dx;
                sensor.y += dy;
            });
    };

    const refreshGraph = async () => {
        if (refreshing || document.hidden || dragging) return;
        refreshing = true;

        try {
            const response = await fetch(window.location.href, {
                credentials: "same-origin",
                cache: "no-store",
                headers: {
                    Accept: "text/html",
                    "X-Requested-With": "fetch",
                },
            });
            const responseUrl = new URL(response.url);
            if (response.status === 401 || responseUrl.pathname === "/") {
                window.location.href = "/";
                return;
            }
            if (!response.ok) return;

            const nextDocument = new DOMParser().parseFromString(
                await response.text(),
                "text/html"
            );
            const nextData = nextDocument.getElementById("tree-graph-data");
            if (!nextData) return;

            const currentSummary = document.querySelector(".tree-toolbar p");
            const nextSummary = nextDocument.querySelector(".tree-toolbar p");
            if (currentSummary && nextSummary) {
                currentSummary.textContent = nextSummary.textContent;
            }

            hydrateGraph(JSON.parse(nextData.textContent));
            dataElement.textContent = nextData.textContent;
            draw();
            if (selected) showDetails(selected);
        } catch {
            // Keep the current graph visible until the next successful poll.
        } finally {
            refreshing = false;
        }
    };

    canvas.addEventListener("pointerdown", event => {
        const box = canvas.getBoundingClientRect();
        pointerX = event.clientX - box.left;
        pointerY = event.clientY - box.top;
        dragging = nodeAt(pointerX, pointerY);
        panning = !dragging;
        moved = false;
        canvas.setPointerCapture(event.pointerId);
    });

    canvas.addEventListener("pointermove", event => {
        const box = canvas.getBoundingClientRect();
        const x = event.clientX - box.left;
        const y = event.clientY - box.top;
        const dx = x - pointerX;
        const dy = y - pointerY;

        if (dragging && dragging.kind !== "server") {
            moveNodeWithSensors(dragging, dx / scale, dy / scale);
            moved = true;
        } else if (panning) {
            offsetX += dx;
            offsetY += dy;
            moved = moved || Math.abs(dx) + Math.abs(dy) > 1;
        } else {
            hovered = nodeAt(x, y);
            canvas.style.cursor = hovered ? "pointer" : "grab";
        }

        pointerX = x;
        pointerY = y;
        draw();
    });

    canvas.addEventListener("pointerup", event => {
        if (!moved) {
            selected = nodeAt(pointerX, pointerY);
            showDetails(selected);
        }
        dragging = null;
        panning = false;
        canvas.releasePointerCapture(event.pointerId);
        draw();
    });

    canvas.addEventListener("wheel", event => {
        event.preventDefault();
        const box = canvas.getBoundingClientRect();
        const x = event.clientX - box.left;
        const y = event.clientY - box.top;
        const before = graphPosition(x, y);
        scale = Math.min(2.4, Math.max(0.3, scale * (event.deltaY > 0 ? 0.9 : 1.1)));
        offsetX = x - before.x * scale;
        offsetY = y - before.y * scale;
        draw();
    }, { passive: false });

    search.addEventListener("input", draw);
    resetButton.addEventListener("click", resetView);
    window.addEventListener("resize", () => {
        resize();
        resetView();
    });

    hydrateGraph(graph);
    resize();
    resetView();
    window.setInterval(refreshGraph, refreshIntervalMs);
})();
