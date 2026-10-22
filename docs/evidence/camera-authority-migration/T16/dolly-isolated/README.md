# T16 dolly perspective probe

Controlled Vulkan raw-RGBA renders isolate the title plane and foreground
subject from T16. Only camera Z changes; the image layers, transforms,
1920×1080 output, and backend stay fixed. Baseline camera Z is -1000 and dolly
camera Z is -1500.

At title depth 0, perspective predicts a scale ratio of 1000/1500 = 0.6667.
The nonzero-alpha title bbox changes from 1837×464 px to 1226×309 px, ratios
0.6674×0.6659.

At subject depth -99, camera-to-plane distances change from 901 to 1401, giving
a predicted ratio of 0.6431. The centered subject bbox width changes from 1086
to 699 px, ratio 0.6436. Its vertical bbox reaches the frame edge in the
baseline, so height is not used as a scale measurement.

Contact sheet: `camera-dolly-isolated-contact.jpg`. Raw frames, plans, PNG
projections, timing sidecars, and logs are beside this file. The centered
subject probe changes only its X placement to keep the silhouette inside the
frame; it does not alter source scale or depth.

This closes the focused CAM-02 perspective-scale gate. It does not certify the
complete T16 sequence or its remaining optical and presentation gates.
