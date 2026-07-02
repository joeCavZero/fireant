(() => {
    let renderers = [];
    let resizeFrame = null;

    const colors = [
        "#ff5c35",
        "#38bdf8",
        "#a78bfa",
        "#22c55e",
        "#fbbf24",
        "#f472b6",
        "#2dd4bf",
        "#fb7185",
        "#818cf8",
        "#84cc16",
    ];
    const gridColor = "rgba(154, 167, 184, 0.16)";
    const textColor = "#9aa7b8";
    const strongTextColor = "#f8fafc";
    const font = "12px Inter, system-ui, sans-serif";

    const groupBy = (items, key) => {
        const groups = new Map();
        items.forEach(item => {
            const value = key(item);
            if (!groups.has(value)) groups.set(value, []);
            groups.get(value).push(item);
        });
        return groups;
    };

    const formatNumber = value => new Intl.NumberFormat("en-US", {
        maximumFractionDigits: 3,
    }).format(value);

    const formatDate = value => new Intl.DateTimeFormat("en-US", {
        month: "short",
        day: "numeric",
        hour: "2-digit",
        minute: "2-digit",
    }).format(new Date(value));

    const prepareCanvas = canvas => {
        const box = canvas.getBoundingClientRect();
        const ratio = window.devicePixelRatio || 1;
        const width = Math.max(1, box.width);
        const height = Math.max(1, box.height);
        canvas.width = Math.round(width * ratio);
        canvas.height = Math.round(height * ratio);
        const context = canvas.getContext("2d");
        context.setTransform(ratio, 0, 0, ratio, 0, 0);
        context.clearRect(0, 0, width, height);
        context.font = font;
        return { context, width, height };
    };

    const niceRange = values => {
        let minimum = Math.min(...values);
        let maximum = Math.max(...values);
        if (minimum === maximum) {
            const padding = Math.abs(minimum) * 0.1 || 1;
            minimum -= padding;
            maximum += padding;
        } else {
            const padding = (maximum - minimum) * 0.08;
            minimum -= padding;
            maximum += padding;
        }
        return { minimum, maximum };
    };

    const drawAxes = (context, plot, range, yTicks = 4) => {
        context.strokeStyle = gridColor;
        context.fillStyle = textColor;
        context.lineWidth = 1;
        context.textAlign = "right";
        context.textBaseline = "middle";

        for (let index = 0; index <= yTicks; index += 1) {
            const ratio = index / yTicks;
            const y = plot.top + plot.height * ratio;
            const value = range.maximum - (range.maximum - range.minimum) * ratio;
            context.beginPath();
            context.moveTo(plot.left, y);
            context.lineTo(plot.left + plot.width, y);
            context.stroke();
            context.fillText(formatNumber(value), plot.left - 10, y);
        }
    };

    const addLegend = (element, items) => {
        element.replaceChildren();
        items.forEach((item, index) => {
            const entry = document.createElement("span");
            const marker = document.createElement("i");
            const label = document.createElement("span");
            marker.style.background = colors[index % colors.length];
            label.textContent = item;
            entry.append(marker, label);
            element.append(entry);
        });
    };

    const registerRenderer = renderer => {
        renderers.push(renderer);
        renderer();
    };

    const roundedRect = (context, x, y, width, height, radius = 0) => {
        if (typeof context.roundRect === "function") {
            context.roundRect(x, y, width, height, radius);
            return;
        }

        // Older browsers and embedded WebViews don't implement roundRect.
        context.rect(x, y, width, height);
    };

    const getReadings = () => {
        const dataElement = document.getElementById("dashboard-chart-data");
        if (!dataElement) return [];

        try {
            return JSON.parse(dataElement.textContent);
        } catch {
            return [];
        }
    };

    const createTrendCharts = readings => {
        const container = document.getElementById("trend-charts");
        if (!container) return;

        container.replaceChildren();
        const byUnit = groupBy(readings, item => item.unit || "unitless");

        [...byUnit.entries()].forEach(([unit, unitReadings]) => {
            const card = document.createElement("article");
            card.className = "dashboard-card chart-card";
            card.innerHTML = `
                <div class="chart-card-header">
                    <div>
                        <span>Line chart</span>
                        <h3></h3>
                    </div>
                    <small>Values are grouped by unit to preserve a meaningful scale.</small>
                </div>
                <div class="chart-canvas-shell">
                    <canvas></canvas>
                </div>
                <div class="chart-legend"></div>
            `;
            container.append(card);

            const canvas = card.querySelector("canvas");
            const legend = card.querySelector(".chart-legend");
            card.querySelector("h3").textContent = `Sensor trends · ${unit}`;
            canvas.setAttribute(
                "aria-label",
                `Time series chart for ${unit} readings`
            );
            const series = groupBy(
                unitReadings,
                item => `${item.node} / ${item.sensor}`
            );
            addLegend(legend, [...series.keys()]);

            registerRenderer(() => {
                const { context, width, height } = prepareCanvas(canvas);
                const plot = {
                    left: 68,
                    top: 18,
                    width: Math.max(10, width - 88),
                    height: Math.max(10, height - 58),
                };
                const values = unitReadings.map(item => item.value);
                const range = niceRange(values);
                const times = unitReadings.map(item => new Date(item.timestamp).getTime());
                const minimumTime = Math.min(...times);
                const maximumTime = Math.max(...times);
                const timeSpan = Math.max(1, maximumTime - minimumTime);

                drawAxes(context, plot, range);

                context.fillStyle = textColor;
                context.textBaseline = "top";
                context.textAlign = "left";
                context.fillText(formatDate(minimumTime), plot.left, plot.top + plot.height + 12);
                context.textAlign = "right";
                context.fillText(
                    formatDate(maximumTime),
                    plot.left + plot.width,
                    plot.top + plot.height + 12
                );

                [...series.entries()].forEach(([name, points], seriesIndex) => {
                    const color = colors[seriesIndex % colors.length];
                    const ordered = [...points].sort(
                        (left, right) => new Date(left.timestamp) - new Date(right.timestamp)
                    );

                    context.beginPath();
                    ordered.forEach((point, index) => {
                        const time = new Date(point.timestamp).getTime();
                        const x = plot.left + ((time - minimumTime) / timeSpan) * plot.width;
                        const y = plot.top + (
                            (range.maximum - point.value) /
                            (range.maximum - range.minimum)
                        ) * plot.height;
                        if (index === 0) context.moveTo(x, y);
                        else context.lineTo(x, y);
                    });
                    context.strokeStyle = color;
                    context.lineWidth = 2;
                    context.lineJoin = "round";
                    context.lineCap = "round";
                    context.stroke();

                    ordered.forEach(point => {
                        const time = new Date(point.timestamp).getTime();
                        const x = plot.left + ((time - minimumTime) / timeSpan) * plot.width;
                        const y = plot.top + (
                            (range.maximum - point.value) /
                            (range.maximum - range.minimum)
                        ) * plot.height;
                        context.beginPath();
                        context.arc(x, y, ordered.length > 80 ? 1.5 : 2.5, 0, Math.PI * 2);
                        context.fillStyle = color;
                        context.fill();
                    });
                });
            });
        });
    };

    const drawSensorVolume = readings => {
        const canvas = document.getElementById("sensor-volume-chart");
        if (!canvas) return;

        const counts = [...groupBy(
            readings,
            item => `${item.node} / ${item.sensor}`
        )].map(([label, items]) => ({ label, value: items.length }))
            .sort((left, right) => right.value - left.value)
            .slice(0, 10);

        registerRenderer(() => {
            const { context, width, height } = prepareCanvas(canvas);
            const left = Math.min(150, Math.max(90, width * 0.32));
            const top = 12;
            const bottom = 20;
            const gap = 9;
            const rowHeight = (height - top - bottom - gap * (counts.length - 1)) /
                Math.max(1, counts.length);
            const maximum = Math.max(...counts.map(item => item.value), 1);
            const availableWidth = Math.max(20, width - left - 55);

            counts.forEach((item, index) => {
                const y = top + index * (rowHeight + gap);
                const barWidth = (item.value / maximum) * availableWidth;
                context.fillStyle = textColor;
                context.textAlign = "right";
                context.textBaseline = "middle";
                context.fillText(item.label, left - 10, y + rowHeight / 2);

                const gradient = context.createLinearGradient(left, 0, left + barWidth, 0);
                gradient.addColorStop(0, "#ff5c35");
                gradient.addColorStop(1, "#ff9a62");
                context.fillStyle = "rgba(154, 167, 184, 0.1)";
                context.beginPath();
                roundedRect(context, left, y, availableWidth, rowHeight, 7);
                context.fill();
                context.fillStyle = gradient;
                context.beginPath();
                roundedRect(context, left, y, Math.max(3, barWidth), rowHeight, 7);
                context.fill();

                context.fillStyle = strongTextColor;
                context.textAlign = "left";
                context.fillText(String(item.value), left + barWidth + 8, y + rowHeight / 2);
            });
        });
    };

    const drawNodeShare = readings => {
        const canvas = document.getElementById("node-share-chart");
        const legend = document.getElementById("node-share-legend");
        if (!canvas || !legend) return;

        const counts = [...groupBy(readings, item => item.node)]
            .map(([label, items]) => ({ label, value: items.length }))
            .sort((left, right) => right.value - left.value);
        const total = readings.length;
        addLegend(
            legend,
            counts.map(item => `${item.label} · ${item.value}`)
        );

        registerRenderer(() => {
            const { context, width, height } = prepareCanvas(canvas);
            const centerX = width / 2;
            const centerY = height / 2;
            const radius = Math.max(35, Math.min(width, height) * 0.32);
            const innerRadius = radius * 0.58;
            let angle = -Math.PI / 2;

            counts.forEach((item, index) => {
                const nextAngle = angle + (item.value / total) * Math.PI * 2;
                context.beginPath();
                context.arc(centerX, centerY, radius, angle, nextAngle);
                context.arc(centerX, centerY, innerRadius, nextAngle, angle, true);
                context.closePath();
                context.fillStyle = colors[index % colors.length];
                context.fill();
                angle = nextAngle;
            });

            context.fillStyle = strongTextColor;
            context.textAlign = "center";
            context.textBaseline = "middle";
            context.font = "700 28px Inter, system-ui, sans-serif";
            context.fillText(String(total), centerX, centerY - 7);
            context.fillStyle = textColor;
            context.font = font;
            context.fillText("readings", centerX, centerY + 18);
        });
    };

    const drawActivity = readings => {
        const canvas = document.getElementById("activity-chart");
        if (!canvas) return;

        const times = readings.map(item => new Date(item.timestamp).getTime());
        const minimumTime = Math.min(...times);
        const maximumTime = Math.max(...times);
        const bucketCount = Math.min(20, Math.max(5, Math.ceil(Math.sqrt(readings.length))));
        const timeSpan = Math.max(1, maximumTime - minimumTime);
        const buckets = Array.from({ length: bucketCount }, (_, index) => ({
            start: minimumTime + (index / bucketCount) * timeSpan,
            value: 0,
        }));

        times.forEach(time => {
            const index = Math.min(
                bucketCount - 1,
                Math.floor(((time - minimumTime) / timeSpan) * bucketCount)
            );
            buckets[index].value += 1;
        });

        registerRenderer(() => {
            const { context, width, height } = prepareCanvas(canvas);
            const plot = {
                left: 54,
                top: 18,
                width: Math.max(10, width - 74),
                height: Math.max(10, height - 58),
            };
            const maximum = Math.max(...buckets.map(bucket => bucket.value), 1);
            const range = { minimum: 0, maximum };
            drawAxes(context, plot, range);

            const gap = Math.max(2, plot.width * 0.006);
            const barWidth = plot.width / bucketCount;
            buckets.forEach((bucket, index) => {
                const heightRatio = bucket.value / maximum;
                const barHeight = Math.max(bucket.value ? 3 : 0, heightRatio * plot.height);
                const x = plot.left + index * barWidth + gap / 2;
                const y = plot.top + plot.height - barHeight;
                const gradient = context.createLinearGradient(0, y, 0, plot.top + plot.height);
                gradient.addColorStop(0, "#ff5c35");
                gradient.addColorStop(1, "rgba(255, 92, 53, 0.25)");
                context.fillStyle = gradient;
                context.beginPath();
                roundedRect(
                    context,
                    x,
                    y,
                    Math.max(1, barWidth - gap),
                    barHeight,
                    [5, 5, 0, 0]
                );
                context.fill();
            });

            context.fillStyle = textColor;
            context.textBaseline = "top";
            context.textAlign = "left";
            context.fillText(formatDate(minimumTime), plot.left, plot.top + plot.height + 12);
            context.textAlign = "right";
            context.fillText(
                formatDate(maximumTime),
                plot.left + plot.width,
                plot.top + plot.height + 12
            );
        });
    };

    const renderDashboard = () => {
        const readings = getReadings();
        renderers = [];
        if (!readings.length) return;

        createTrendCharts(readings);
        drawSensorVolume(readings);
        drawNodeShare(readings);
        drawActivity(readings);
    };

    window.addEventListener("resize", () => {
        if (resizeFrame) cancelAnimationFrame(resizeFrame);
        resizeFrame = requestAnimationFrame(() => {
            renderers.forEach(renderer => renderer());
        });
    });

    renderDashboard();
    window.addEventListener("live-page:update", renderDashboard);
})();
