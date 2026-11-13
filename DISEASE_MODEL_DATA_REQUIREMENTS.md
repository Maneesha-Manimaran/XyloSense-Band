# Disease-prediction training data requirements

There is no disease-labelled training dataset included because none was supplied. Do not manufacture disease labels from the same sensor thresholds used by the model; that would only teach the model to copy those rules and would not validate disease prediction.

For a defensible first model, collect rows with:
- timestamp and tree ID
- tree species and location/season if available
- resistance plus units and calibration details
- temperature, humidity, soil moisture (and sensor metadata)
- verified label from a plant pathologist / trusted field record: healthy, or a named disease confirmed by an appropriate method
- label date and how the label was verified

Split train/test by tree or by time where possible, so readings from the same tree do not leak into both sets. Evaluate precision, recall, F1 and confusion matrix. If only “healthy / stressed” labels are reliably available, train a stress-risk classifier instead of claiming a specific disease diagnosis.

The CSV in `backend/data/` is synthetic and intentionally has no disease labels. It is for testing the Spark cleaning and baseline/deviation code only.
