# Ideas for v0.2:
 - per cell and global enable/disable label
 - right click on the preview with an option to "copy image" that will copy the preview image to the clipboad.
 - Dive profile graphic
 - tank pressure split into different available tanks.
 - add the possibility to manually set an export zone on the timeline.
 - Support UDDF dive logs (see section called UDDF)


## UDDF

As one area for potential improvement you could look into adding support
for reading the dive profile information from UDDF
(https://wrobell.dcmod.org/uddf/) - a lot of dive log applications
support data export / import in this format, including Subsurface
(https://github.com/subsurface/subsurface/blob/master/xslt/uddf.xslt) 


# Marketing campaign:

Absolutely, here's a practical playbook for getting the word out with zero budget:

## Where to post

### Scuba diving communities (your target audience)
- Reddit: r/scuba, r/scubadiving, r/underwatervideography — post a short demo video or before/after comparison showing an overlay on actual footage. Reddit loves "I built this" posts. Don't be salesy, just share what you made and why.
- ScubaBoard.com forums — large active community, post in the equipment/software section
- Wetpixel.com — focused on underwater imaging, very relevant audience

### Open source / developer communities
- Reddit: r/opensource, r/linux, r/kde (since it's Qt-based), r/cpp
- Hacker News (news.ycombinator.com) — "Show HN: Unabara — open-source telemetry overlays for scuba diving videos". HN loves niche, well-crafted open source tools.
- Lobste.rs — similar to HN, smaller but engaged

### Diving-specific social media
- Facebook groups: Search for scuba diving video/photography groups — there are several with tens of thousands of members
- YouTube: Record a short (2-3 min) tutorial showing the workflow from dive log to finished overlay. This becomes evergreen content people find when searching "dive telemetry overlay"

### Package/app listings
- Flathub — if you publish there, it gives you visibility to all Linux users browsing the store
- GitHub Topics — make sure your repo has relevant topics: scuba, diving, overlay, telemetry, subsurface, qt6

## What to post

The most effective format is a short demo — either a GIF/video showing a dive clip with the overlay, or a before/after screenshot. People scroll past text but stop for visuals.

A good first post template:

```I built an open-source app to add telemetry overlays (depth, temperature, tank pressure, NDL) to scuba diving videos. It imports Subsurface dive logs and auto-syncs with GoPro footage using timecodes. Free, cross-platform, no account needed.```

[screenshot or short video clip]

GitHub: [link]

## Tips
- Post at different times — don't spam everything the same day. Space it out over 1-2 weeks.
- Engage with replies — answer questions, take feature requests. This builds goodwill and boosts visibility on algorithmic platforms.
- Cross-post the YouTube video — once you have a demo video, link it everywhere. It's the single most reusable asset.
- Ask the Subsurface community — since Unabara reads their format, the Subsurface user mailing list or forum might be interested. It's complementary, not competitive.
- GitHub README matters — many people will land on your repo first. The screenshot you already have is good. A short GIF showing the overlay in action would be even better.

The single highest-impact thing you can do: record a 60-second screen capture showing a real dive with the overlay, post it on r/scuba with a genuine "I built this" title. Divers who shoot video will immediately understand the value.