(() => {
    const refreshIntervalMs = 5000;
    const liveRoot = document.querySelector("[data-live-page]");
    if (!liveRoot) return;

    let refreshing = false;

    const replaceLiveContent = html => {
        const nextDocument = new DOMParser().parseFromString(html, "text/html");
        const currentRoot = document.querySelector("[data-live-page]");
        const nextRoot = nextDocument.querySelector(
            `[data-live-page="${currentRoot.dataset.livePage}"]`
        );
        if (!currentRoot || !nextRoot) {
            window.location.href = "/";
            return;
        }

        currentRoot.replaceWith(nextRoot);

        nextDocument
            .querySelectorAll("script[type='application/json'][data-live-data]")
            .forEach(nextData => {
                const currentData = document.getElementById(nextData.id);
                if (currentData) currentData.replaceWith(nextData);
                else document.body.append(nextData);
            });

        window.dispatchEvent(new CustomEvent("live-page:update"));
    };

    const refresh = async () => {
        if (refreshing || document.hidden) return;
        const currentRoot = document.querySelector("[data-live-page]");
        if (
            currentRoot &&
            currentRoot.contains(document.activeElement) &&
            ["INPUT", "SELECT", "TEXTAREA"].includes(document.activeElement.tagName)
        ) {
            return;
        }
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
            if (response.ok) replaceLiveContent(await response.text());
        } catch {
            // Keep the last successful render visible until the next poll.
        } finally {
            refreshing = false;
        }
    };

    window.setInterval(refresh, refreshIntervalMs);
})();
