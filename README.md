# XylosenseBand — staged project starter

## Chosen IDE
Use **IntelliJ IDEA** as the main IDE because your faculty requested it. You can edit the HTML/CSS/JavaScript frontend and Scala/Spark backend in the same project. VS Code is optional, not required.

## What is included
- `frontend/`: your supplied `index.html` and `script.js`, plus a basic `style.css`.
- `backend/`: a Scala + Apache Spark SQL/MLlib sbt project.
- `backend/data/sensor_readings_demo.csv`: clearly marked synthetic demo rows for testing the Spark pipeline only.
- `backend/src/main/scala/.../SensorDataProcessor.scala`: reads the CSV, cleans invalid records, calculates a tree-specific baseline from rows marked `baseline_sample=true`, calculates resistance deviation, and writes summary outputs.
- `hardware/plant_health_monitor_reference.ino`: the supplied Arduino sketch copied as a reference. Wi-Fi credentials were replaced with placeholders.
- `docs/DISEASE_MODEL_DATA_REQUIREMENTS.md`: why verified labels are required before claiming disease prediction.

## Important status: not yet a connected live system
The existing dashboard JavaScript currently contains hard-coded/demo sensor values and a demo timer. It is **not connected** to the Scala backend yet.
The Arduino sketch serves its own local web page and `/data` endpoint. It does not currently send sensor readings to the laptop's Scala backend. That integration is a later step.
The Arduino sketch's browser refresh uses `setInterval(u, 3000)` (requests display data every 3 seconds), while sensor sampling is configured with `SENSOR_INTERVAL_MS 5000UL` (every 5 seconds). These are not the same interval. Hardware must be connected and online for actual live values.

## Java / Scala / Spark compatibility
Java 25 is installed on your laptop. Do not uninstall it. Spark 3.5.x is commonly used with Java 8/11/17 and supports Java 17; Java 25 is not the safest baseline for this starter. Before running the backend, install a side-by-side JDK 17 (keep Java 25), then set IntelliJ's Project SDK and sbt JVM to JDK 17. Confirm `sbt --version` separately in the terminal. Do not type multiple commands on the same line.

## First run — Spark pipeline only
1. Open the `XylosenseBand_Starter` folder in IntelliJ.
2. Open the `backend` folder as an sbt project (or attach/import it as a module).
3. Set Project SDK / sbt JVM to JDK 17.
4. In the `backend` terminal, run:
   `sbt run`
5. The processor reads `data/sensor_readings_demo.csv` and writes Spark CSV output directories under `output/`.
6. Do not present these synthetic outputs as real sensor measurements or disease-model accuracy.

## Planned integration stages
1. Get the Spark CSV pipeline running.
2. Inspect and validate the Arduino code / sensor wiring and actual sampling interval.
3. Make ESP32 send timestamped readings to a Scala HTTP API over the same Wi-Fi network.
4. Store each real reading and prediction history (Firebase can be integrated later; no Firebase credentials are included).
5. Connect the dashboard to the Scala API and remove simulated readings from live mode.
6. Collect tree-specific readings with expert-verified labels.
7. Only then train and evaluate a disease-risk classifier. Until verified labels exist, use “monitoring flag / stress-risk indicator,” not “disease confirmed.”

## Demo dashboard login warning
The supplied frontend has a client-side demo login (`admin` / `1234`). This is not secure authentication and should be replaced with server-side authentication before any real deployment.
