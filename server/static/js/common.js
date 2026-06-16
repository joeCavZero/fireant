(() => {
    const copyWithFallback = value => {
        const input = document.createElement("textarea");
        input.value = value;
        input.setAttribute("readonly", "");
        input.style.position = "fixed";
        input.style.opacity = "0";
        document.body.append(input);
        input.select();

        try {
            return document.execCommand("copy");
        } finally {
            input.remove();
        }
    };

    const copyText = async value => {
        if (navigator.clipboard && window.isSecureContext) {
            await navigator.clipboard.writeText(value);
            return true;
        }
        return copyWithFallback(value);
    };

    document.addEventListener("click", async event => {
        const button = event.target.closest(".button-copy[data-copy-value]");
        if (!button) return;

        const originalLabel = button.textContent;
        button.classList.remove("is-copied", "is-error");

        try {
            const copied = await copyText(button.dataset.copyValue || "");
            if (!copied) throw new Error("Clipboard operation failed");
            button.textContent = "Copied";
            button.classList.add("is-copied");
        } catch {
            button.textContent = "Copy failed";
            button.classList.add("is-error");
        }

        window.setTimeout(() => {
            button.textContent = originalLabel;
            button.classList.remove("is-copied", "is-error");
        }, 1600);
    });
})();
