/* =====================================================
   XYLOSENSE BAND DASHBOARD
   ===================================================== */


/* =====================================================
   LOGIN SETTINGS
   CHANGE USERNAME AND PASSWORD HERE
   ===================================================== */

const LOGIN_USERNAME = "admin";
const LOGIN_PASSWORD = "1234";


/* =====================================================
   SENSOR DATA
   ===================================================== */

let temperature = null;
let humidity = null;
let moisture = null;
let resistance = null;
let battery = null;
let lastEspReading = null;


/* =====================================================
   HISTORICAL DATA
   ===================================================== */

let labels = [];
let temperatureData = [];
let humidityData = [];
let moistureData = [];
let resistanceData = [];
let batteryData = [];
let healthData = [];


let demoTimer = null;


/* =====================================================
   LOGIN
   ===================================================== */

const loginForm = document.getElementById("loginForm");

if (loginForm) {
loginForm.addEventListener("submit", function(event) {

        event.preventDefault();

        const username =
            document.getElementById("username").value;

        const password =
            document.getElementById("password").value;

        const error =
            document.getElementById("loginError");


        if (
            username === LOGIN_USERNAME &&
            password === LOGIN_PASSWORD
        ) {

            document
                .getElementById("loginPage")
                .classList.add("hidden");

            document
                .getElementById("dashboardPage")
                .classList.remove("hidden");

            error.innerText = "";

            initializeCharts();

            updateDashboard();

        } else {

            error.innerText =
                "Invalid username or password.";

        }

    });
}


/* =====================================================
   LOGOUT
   ===================================================== */

function logout() {

    if (demoTimer) {

        clearInterval(demoTimer);

        demoTimer = null;

    }

    document
        .getElementById("dashboardPage")
        .classList.add("hidden");

    document
        .getElementById("loginPage")
        .classList.remove("hidden");

    document
        .getElementById("username")
        .value = "";

    document
        .getElementById("password")
        .value = "";

}


/* =====================================================
   CALCULATE HEALTH
   ===================================================== */

function calculateHealth() {

    let health = 100;


    if (temperature > 35) {
        health -= 15;
    }

    if (temperature < 15) {
        health -= 10;
    }


    if (humidity < 40 || humidity > 90) {
        health -= 10;
    }


    if (moisture < 30) {
        health -= 20;
    }


    if (resistance > 20) {
        health -= 20;
    }


    if (battery < 20) {
        health -= 5;
    }


    return Math.max(0, health);
}


/* =====================================================
   UPDATE DASHBOARD
   ===================================================== */

function updateDashboard() {

    const health =
        calculateHealth();


    const setText = (id, value) => {
        const element = document.getElementById(id);
        if (element) element.innerText = value;
    };

    setText("temperature", temperature === null ? "--" : temperature.toFixed(1) + " °C");
    setText("humidity", humidity === null ? "--" : humidity.toFixed(0) + " %");
    setText("moisture", moisture === null ? "--" : moisture.toFixed(0) + " %");
    setText("resistance", resistance === null ? "--" : resistance.toFixed(1) + " kΩ");
    setText("battery", battery === null ? "--" : battery.toFixed(0) + " %");
    setText("health", healthData.length ? healthData[healthData.length - 1] + " %" : "--");
    setText("deviceBattery", battery === null ? "--" : battery.toFixed(0) + " %");
    setText("deviceHealth", healthData.length ? healthData[healthData.length - 1] + " %" : "--");


    updateStatus(health);

    updateAnalytics();

    updateAlerts();

    updateCharts();


    const lastUpdate = document.getElementById("lastUpdate");
    if (lastUpdate) lastUpdate.innerText = new Date().toLocaleTimeString();

}


/* =====================================================
   STATUS
   ===================================================== */

function updateStatus(health) {

    const status =
        document.getElementById("status");

    const message =
        document.getElementById("statusMessage");


    if (!status || !message) return;

    status.className = "status";


    if (health >= 70) {

        status.innerText = "NORMAL";

        status.classList.add("normal");

        message.innerText =
            "Environmental conditions are currently within the monitored range.";

    }

    else if (health >= 40) {

        status.innerText = "WARNING";

        status.classList.add("warning");

        message.innerText =
            "Some environmental values require attention.";

    }

    else {

        status.innerText = "CRITICAL";

        status.classList.add("danger");

        message.innerText =
            "Critical environmental conditions detected.";

    }

}


/* =====================================================
   ANALYTICS
   ===================================================== */

function average(array) {
    if (!array || array.length === 0) return 0;
    return array.reduce((a, b) => a + b, 0) / array.length;
}


function updateAnalytics() {
    const setText = (id, value) => {
        const element = document.getElementById(id);
        if (element) element.innerText = value;
    };

    setText("avgTemperature", average(temperatureData).toFixed(1) + " °C");
    setText("avgHumidity", average(humidityData).toFixed(0) + " %");
    setText("avgMoisture", average(moistureData).toFixed(0) + " %");
    setText("avgHealth", average(healthData).toFixed(0) + " / 100");
}


/* =====================================================
   ALERTS
   ===================================================== */

function updateAlerts() {

    const alerts =
        document.getElementById("alertsList");

    if (!alerts) return;

    const latestHealth =
        calculateHealth();


    let messages = [];


    if (temperature > 35) {

        messages.push({
            type: "warning",
            title: "High Temperature",
            text: "Temperature is above the configured range."
        });

    }


    if (humidity < 40) {

        messages.push({
            type: "warning",
            title: "Low Humidity",
            text: "Humidity is below the configured range."
        });

    }


    if (moisture < 30) {

        messages.push({
            type: "warning",
            title: "Low Soil Moisture",
            text: "Soil moisture level is low."
        });

    }


    if (resistance > 20) {

        messages.push({
            type: "danger",
            title: "Resistance Abnormal",
            text: "Measured resistance is higher than the configured range."
        });

    }


    if (battery < 20) {

        messages.push({
            type: "danger",
            title: "Low Battery",
            text: "Battery level is low."
        });

    }


    if (latestHealth >= 70 &&
        messages.length === 0) {

        messages.push({
            type: "success",
            title: "System Normal",
            text: "All monitored values are within normal range."
        });

    }


    alerts.innerHTML = "";


    messages.forEach(function(item) {

        const div =
            document.createElement("div");

        div.className =
            "alert-item " + item.type;

        div.innerHTML = `
            <strong>${item.title}</strong>
            <span>${item.text}</span>
        `;

        alerts.appendChild(div);

    });


    const alertMessage =
        document.getElementById("alertMessage");


    if (messages[0].type === "success") {

        alertMessage.innerText =
            "✓ No critical alerts";

        alertMessage.className =
            "alert normal-alert";

    }

    else {

        alertMessage.innerText =
            "⚠ " + messages[0].title;

        alertMessage.className =
            "alert normal-alert";

    }

}


/* =====================================================
   CHART VARIABLES
   ===================================================== */

let temperatureChart;
let humidityChart;
let moistureChart;
let resistanceChart;
let batteryChart;
let healthChart;
let combinedChart;


/* =====================================================
   INITIALIZE CHARTS
   ===================================================== */

function initializeCharts() {

    const charts = [temperatureChart, humidityChart, moistureChart, healthChart];
    charts.forEach(chart => {
        if (chart) chart.destroy();
    });


    temperatureChart =
        createChart(
            "temperatureChart",
            "Temperature (°C)",
            temperatureData
        );


    humidityChart =
        createChart(
            "humidityChart",
            "Humidity (%)",
            humidityData
        );


    moistureChart =
        createChart(
            "soilChart",
            "Soil Moisture (%)",
            moistureData
        );


    healthChart =
        createChart(
            "healthChart",
            "Health Index",
            healthData
        );


}


/* =====================================================
   CREATE CHART
   ===================================================== */

function createChart(
    elementId,
    label,
    data
) {

    const canvas =
        document.getElementById(elementId);

    if (!canvas) return null;


    return new Chart(
        canvas,
        {
            type: "line",

            data: {

                labels: labels,

                datasets: [

                    {
                        label: label,

                        data: data,

                        borderWidth: 3,

                        tension: 0.3,

                        fill: false
                    }

                ]

            },

            options: {

                responsive: true,

                maintainAspectRatio: false,

                plugins: {

                    legend: {
                        display: true
                    }

                },

                scales: {

                    y: {
                        beginAtZero: false
                    }

                }

            }

        }
    );

}


/* =====================================================
   UPDATE CHARTS
   ===================================================== */

function updateCharts() {

    const chartUpdates = [
        [temperatureChart, temperatureData],
        [humidityChart, humidityData],
        [moistureChart, moistureData],
        [healthChart, healthData]
    ];

    chartUpdates.forEach(([chart, data]) => {
        if (!chart) return;
        chart.data.labels = labels;
        chart.data.datasets[0].data = data;
        chart.update();
    });

}


/* =====================================================
   UPDATE TEST VALUES
   ===================================================== */

function updateTestValues() {

    temperature =
        parseFloat(
            document.getElementById(
                "testTemperature"
            ).value
        );


    humidity =
        parseFloat(
            document.getElementById(
                "testHumidity"
            ).value
        );


    moisture =
        parseFloat(
            document.getElementById(
                "testMoisture"
            ).value
        );


    resistance =
        parseFloat(
            document.getElementById(
                "testResistance"
            ).value
        );


    battery =
        parseFloat(
            document.getElementById(
                "testBattery"
            ).value
        );


    addReading();

}


/* =====================================================
   ADD NEW READING
   ===================================================== */

function addReading() {

    const health =
        calculateHealth();


    temperatureData.push(
        temperature
    );

    humidityData.push(
        humidity
    );

    moistureData.push(
        moisture
    );

    resistanceData.push(
        resistance
    );

    batteryData.push(
        battery
    );

    healthData.push(
        health
    );


    labels.push(
        new Date().toLocaleTimeString(
            [],
            {
                hour: "2-digit",
                minute: "2-digit"
            }
        )
    );


    if (labels.length > 10) {

        labels.shift();

        temperatureData.shift();

        humidityData.shift();

        moistureData.shift();

        resistanceData.shift();

        batteryData.shift();

        healthData.shift();

    }


    updateDashboard();

}


/* =====================================================
   AUTOMATIC DEMO
   ===================================================== */

function startDemo() {

    if (demoTimer) {

        clearInterval(demoTimer);

        demoTimer = null;

        return;

    }


    demoTimer =
        setInterval(function() {

            temperature =
                28 + Math.random() * 10;

            humidity =
                50 + Math.random() * 35;

            moisture =
                35 + Math.random() * 40;

            resistance =
                9 + Math.random() * 15;

            battery =
                Math.max(
                    10,
                    battery - 0.1
                );


            document.getElementById(
                "testTemperature"
            ).value =
                temperature.toFixed(1);


            document.getElementById(
                "testHumidity"
            ).value =
                humidity.toFixed(0);


            document.getElementById(
                "testMoisture"
            ).value =
                moisture.toFixed(0);


            document.getElementById(
                "testResistance"
            ).value =
                resistance.toFixed(1);


            document.getElementById(
                "testBattery"
            ).value =
                battery.toFixed(0);


            addReading();

        }, 3000);

}


/* =====================================================
   NAVIGATION
   ===================================================== */

function showSection(sectionId) {

    const sections =
        document.querySelectorAll(
            ".dashboard-section"
        );


    sections.forEach(
        function(section) {

            section.classList.add(
                "hidden-section"
            );

        }
    );


    document
        .getElementById(sectionId)
        .classList.remove(
            "hidden-section"
        );


    const buttons =
        document.querySelectorAll(
            ".nav-item"
        );


    buttons.forEach(
        function(button) {

            button.classList.remove(
                "active"
            );

        }
    );


    event.currentTarget.classList.add(
        "active"
    );

}


/* =====================================================
   FIRESTORE LIVE SENSOR HISTORY / PLOT
   ===================================================== */

window.addEventListener("xylosense-readings-list", function(event) {
    const readings = (event.detail || [])
        .filter(reading => Number.isFinite(reading.timestamp));

    if (readings.length === 0) return;

    readings.sort((a, b) => a.timestamp - b.timestamp);

    labels = readings.slice(-10).map(reading =>
        new Date(reading.timestamp).toLocaleTimeString([], {
            hour: "2-digit",
            minute: "2-digit",
            second: "2-digit"
        })
    );
    temperatureData = readings.slice(-10).map(reading => reading.temperature);
    humidityData = readings.slice(-10).map(reading => reading.humidity);
    moistureData = readings.slice(-10).map(reading => reading.soilMoisture);
    resistanceData = readings.slice(-10).map(reading => reading.resistance);
    healthData = readings.slice(-10).map(reading => reading.healthIndex);

    updateCharts();
});


/* =====================================================
   FIRESTORE LIVE SENSOR STREAM
   ===================================================== */

window.addEventListener("xylosense-reading", function(event) {
    const reading = event.detail;

    if (typeof demoTimer !== "undefined" && demoTimer) {
        clearInterval(demoTimer);
        demoTimer = null;
    }

    temperature = reading.temperature;
    humidity = reading.humidity;
    moisture = reading.soilMoisture;
    resistance = reading.resistance;

    addReading();
    if (healthData.length) {
        healthData[healthData.length - 1] = reading.healthIndex;
    }

    updateDashboard();

    if (document.getElementById("testTemperature")) {
        document.getElementById("testTemperature").value = temperature.toFixed(1);
        document.getElementById("testHumidity").value = humidity.toFixed(0);
        document.getElementById("testMoisture").value = moisture.toFixed(0);
        document.getElementById("testResistance").value = resistance.toFixed(1);
    }

    const treeElement = document.getElementById("treeId");
    if (treeElement) treeElement.textContent = reading.treeId;

    const statusElement = document.getElementById("plantStatus");
    if (statusElement) statusElement.textContent = reading.status;

    console.log("Firestore reading received:", reading);
});

updateDashboard();

/* =====================================================
   DIRECT ESP32 CONNECTION
   ===================================================== */

(function connectEsp32() {
    const ESP32_URL = "http://10.59.179.196/data";
    const pollDelay = 7000;
    let lastSuccessfulReading = null;

    function setConnectionStatus(text, connected) {
        const status = document.getElementById("connectionStatus");
        if (status) {
            status.textContent = text;
            status.className = connected ? "connected" : "disconnected";
        }
        const deviceStatus = document.getElementById("deviceStatus");
        if (deviceStatus) deviceStatus.textContent = connected ? "Connected" : "Offline / stale";
    }

    function finite(value) {
        return typeof value === "number" && Number.isFinite(value);
    }

    function validateEsp32Response(data) {
        return data &&
            data.wifi &&
            data.res &&
            data.env &&
            data.hist &&
            finite(data.res.kohm) &&
            finite(data.res.health) &&
            finite(data.env.temp) &&
            finite(data.env.hum) &&
            finite(data.env.soil) &&
            Array.isArray(data.hist.v);
    }

    function addCurrentReadingToTable(reading) {
        const table = document.getElementById("readingsTableBody");
        if (!table) return;
        table.replaceChildren();
        const row = document.createElement("tr");
        [reading.treeId, reading.temperature.toFixed(1), reading.humidity.toFixed(1),
         reading.soilMoisture.toFixed(0), reading.resistance.toFixed(1),
         reading.healthIndex.toFixed(0), reading.status,
         reading.timestamp.toLocaleString()].forEach(value => {
            const cell = document.createElement("td");
            cell.textContent = String(value);
            row.appendChild(cell);
        });
        table.appendChild(row);
    }

    async function pollEsp32() {
        const controller = new AbortController();
        const timeout = window.setTimeout(() => controller.abort(), 5000);
        try {
            const response = await fetch(ESP32_URL, {
                cache: "no-store",
                signal: controller.signal
            });
            if (!response.ok) throw new Error(`ESP32 returned HTTP ${response.status}`);

            const data = await response.json();
            if (!validateEsp32Response(data)) throw new Error("ESP32 response did not contain the expected wifi/res/env/hist structure");

            const now = new Date();
            const healthIndex = Math.max(0, Math.min(100, data.res.health));
            const reading = {
                treeId: "TREE-01",
                resistance: data.res.kohm,
                temperature: data.env.temp,
                humidity: data.env.hum,
                soilMoisture: data.env.soil,
                healthIndex,
                status: String(data.dec && data.dec.v ? data.dec.v : data.res.cond || "UNKNOWN"),
                timestamp: now
            };

            lastSuccessfulReading = now;
            temperature = reading.temperature;
            humidity = reading.humidity;
            moisture = reading.soilMoisture;
            resistance = reading.resistance;

            labels = data.hist.v.map((_, index) => `Day ${index + 1}`);
            resistanceData = data.hist.v.filter(Number.isFinite);
            temperatureData = [...temperatureData, reading.temperature].slice(-10);
            humidityData = [...humidityData, reading.humidity].slice(-10);
            moistureData = [...moistureData, reading.soilMoisture].slice(-10);
            healthData = [...healthData, reading.healthIndex].slice(-10);
            while (temperatureData.length > labels.length) temperatureData.shift();
            while (humidityData.length > labels.length) humidityData.shift();
            while (moistureData.length > labels.length) moistureData.shift();
            while (healthData.length > labels.length) healthData.shift();

            setConnectionStatus("Connected to ESP32", true);
            const lastUpdated = document.getElementById("lastUpdated");
            if (lastUpdated) lastUpdated.textContent = `Last update: ${now.toLocaleTimeString()}`;
            const treeStatus = document.getElementById("treeStatus");
            if (treeStatus) treeStatus.textContent = reading.status;
            const sensorMode = document.getElementById("sensorMode");
            if (sensorMode) sensorMode.textContent = "Direct ESP32 live data";
            addCurrentReadingToTable(reading);
            updateDashboard();
        } catch (error) {
            const stale = lastSuccessfulReading ? ` (last data ${lastSuccessfulReading.toLocaleTimeString()})` : "";
            setConnectionStatus(`ESP32 offline${stale}`, false);
            const lastUpdated = document.getElementById("lastUpdated");
            if (lastUpdated) lastUpdated.textContent = lastSuccessfulReading
                ? `Last data received: ${lastSuccessfulReading.toLocaleTimeString()}`
                : "Waiting for ESP32 data...";
            const sensorMode = document.getElementById("sensorMode");
            if (sensorMode) sensorMode.textContent = "Direct ESP32 - offline";
            console.warn("ESP32 connection unavailable:", error.message);
        } finally {
            window.clearTimeout(timeout);
        }
    }

    pollEsp32();
    window.setInterval(pollEsp32, pollDelay);
})();

/* =====================================================
   END
   ===================================================== */