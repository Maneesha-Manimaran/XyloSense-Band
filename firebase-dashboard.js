
import { initializeApp } from "https://www.gstatic.com/firebasejs/10.14.1/firebase-app.js";

import {
    getAuth,
    signInAnonymously
} from "https://www.gstatic.com/firebasejs/10.14.1/firebase-auth.js";

import {
    getFirestore,
    collection,
    query,
    orderBy,
    limit,
    onSnapshot,
    addDoc,
    serverTimestamp
} from "https://www.gstatic.com/firebasejs/10.14.1/firebase-firestore.js";

import { firebaseConfig } from "./firebase-config.js";

// --------------------------------------------------
// 1. INITIALIZE FIREBASE
// --------------------------------------------------

const app = initializeApp(firebaseConfig);
const auth = getAuth(app);
const db = getFirestore(app);

const readingsCollection = collection(db, "sensor_readings");

let demoTimer = null;
let demoCounter = 0;
let resistance = 11;
let healthIndex = 95;

// --------------------------------------------------
// 2. HELPER: SHOW FIREBASE ERRORS
// --------------------------------------------------

function showFirebaseError(error) {
    console.error("XyloSenseBand Firebase error:", error);

    window.dispatchEvent(
        new CustomEvent("xylosense-firestore-error", {
            detail: {
                code: error?.code || "unknown",
                message: error?.message || "Unknown Firebase error"
            }
        })
    );
}

// --------------------------------------------------
// 3. CREATE A DEMO SENSOR READING
// --------------------------------------------------
// This is only for testing before real sensors are connected.
// Disable demo mode when real sensor data is available.

async function writeDemoReading() {
    resistance = Math.max(
        8,
        Math.min(25, resistance + (Math.random() - 0.48) * 1.8)
    );

    healthIndex = Math.max(
        30,
        Math.min(
            100,
            Math.round(
                100 -
                Math.max(0, resistance - 10) * 4 +
                (Math.random() - 0.5) * 4
            )
        )
    );

    const reading = {
        treeId: "DEMO-TREE-01",
        resistance: Number(resistance.toFixed(2)),
        temperature: Number((28 + Math.random() * 10).toFixed(1)),
        humidity: Number((50 + Math.random() * 35).toFixed(1)),
        soilMoisture: Number((35 + Math.random() * 40).toFixed(1)),
        healthIndex: healthIndex,
        status:
            healthIndex >= 75
                ? "NORMAL"
                : healthIndex >= 40
                    ? "MEDIUM"
                    : "CRITICAL",
        timestamp: serverTimestamp()
    };

    try {
        const documentRef = await addDoc(readingsCollection, reading);

        demoCounter++;

        console.log(
            `Demo reading #${demoCounter} saved to Firestore. Document ID:`,
            documentRef.id
        );
    } catch (error) {
        showFirebaseError(error);
    }
}

// --------------------------------------------------
// 4. START DEMO MODE
// --------------------------------------------------

function startDemoMode() {
    // Write one reading immediately.
    writeDemoReading();

    // Write another reading every 5 seconds.
    if (demoTimer === null) {
        demoTimer = setInterval(() => {
            writeDemoReading();
        }, 5000);
    }

    console.log("XyloSenseBand demo mode started.");
}

// --------------------------------------------------
// 5. READ THE LATEST 50 FIRESTORE DOCUMENTS
// --------------------------------------------------

function startReadingListener() {
    const readingsQuery = query(
        readingsCollection,
        orderBy("timestamp", "desc"),
        limit(50)
    );

    onSnapshot(
        readingsQuery,
        (snapshot) => {
            const readings = [];

            snapshot.forEach((documentSnapshot) => {
                const data = documentSnapshot.data();

                const timestamp = data.timestamp?.toDate
                    ? data.timestamp.toDate()
                    : null;

                readings.push({
                    id: documentSnapshot.id,
                    treeId: data.treeId ?? data.treeID ?? "TREE-01",
                    resistance: Number(data.resistance ?? 0),
                    temperature: Number(data.temperature ?? 0),
                    humidity: Number(data.humidity ?? 0),
                    soilMoisture: Number(data.soilMoisture ?? 0),
                    healthIndex: Number(data.healthIndex ?? 0),
                    status: String(data.status ?? "UNKNOWN"),
                    timestamp: timestamp ? timestamp.getTime() : null
                });
            });

            // Send all retrieved readings to the HTML table.
            window.dispatchEvent(
                new CustomEvent("xylosense-readings-list", {
                    detail: readings
                })
            );

            // Send the latest reading to the existing dashboard cards.
            if (readings.length > 0) {
                const latest = readings[0];

                window.dispatchEvent(
                    new CustomEvent("xylosense-reading", {
                        detail: {
                            ...latest,
                            timestamp: latest.timestamp
                                ? new Date(latest.timestamp)
                                : null
                        }
                    })
                );
            }

            console.log(
                `Firestore updated: ${readings.length} readings received.`
            );
        },
        (error) => {
            showFirebaseError(error);
        }
    );
}

// --------------------------------------------------
// 6. SIGN IN AND START THE APPLICATION
// --------------------------------------------------

async function startXyloSenseBand() {
    try {
        await signInAnonymously(auth);

        if (!auth.currentUser) {
            throw new Error("Anonymous authentication completed without a signed-in user.");
        }

        console.log("Firebase Anonymous Authentication successful.", auth.currentUser.uid);

        // Register the listener only after authentication is confirmed.
        startReadingListener();

        // Demo writes are disabled: ESP32 readings are ingested by Scala/Spark
        // and published to Firestore by the backend service account.
        console.log("XyloSenseBand live mode ready; waiting for backend data.");

    } catch (error) {
        showFirebaseError(error);
    }
}

startXyloSenseBand();
