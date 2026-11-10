# Independent project branding

`project-logo.png` is the supplied project steel lettering edited with the built-in imagegen tool on 14 October 2026. It has transparent alpha and is embedded in the launcher. It is separate from artwork extracted from an installed game. The supplied project icon remains unchanged.

Final prompt:

> Edit target: the supplied JAILBREAK RECOMP steel logo image. Remove the entire blue background, including blue in the holes of letters, leaving genuine transparent alpha. Preserve the exact JAILBREAK and RECOMP text, metallic riveted letter shapes, steel texture, rust specks, perspective and wings. No redesign, no added text, no colored backdrop or checkerboard. Keep crisp high resolution edges and a modest neutral black drop shadow tight to lettering. Crop composition to the logo with small transparent padding. This is a transparent launcher logo asset, not a mockup.

WIC loads premultiplied BGRA; the launcher uses AlphaBlend. It never treats a blue color key as transparency. Original game artwork is loaded only from a validated installed image and is excluded from the public archive.
