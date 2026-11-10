// Aircraft-overhead feed (feed_aircraft.c): the values it publishes, for the designer.
// VALUES go in the "Insert live value" menu; SAMPLES fill the preview when the
// globe hasn't published a real value yet. Details in docs/aircraft.md.

export const VALUES = [
  { group: 'Aircraft', name: 'planeline', label: 'Nearest flight, one line (UAL123 SFO-ORD B738)' },
  { group: 'Aircraft', name: 'planewhere', label: 'Where to look (LOOK NE 2.4 NM 10800 FT)' },
  { group: 'Aircraft', name: 'planetrip', label: 'Trip in words (TO CHICAGO, FROM DENVER, HEADING SW)' },
  { group: 'Aircraft', name: 'plane', label: 'Flight, or registration (UAL123)' },
  { group: 'Aircraft', name: 'planeroute', label: 'Route (SFO-ORD)' },
  { group: 'Aircraft', name: 'planefrom', label: 'From airport (SFO)' },
  { group: 'Aircraft', name: 'planeto', label: 'To airport (ORD)' },
  { group: 'Aircraft', name: 'planetype', label: 'Aircraft type (B738)' },
  { group: 'Aircraft', name: 'planealt', label: 'Altitude in feet (10800)' },
  { group: 'Aircraft', name: 'planespd', label: 'Ground speed in knots (314)' },
  { group: 'Aircraft', name: 'planedist', label: 'Distance in nautical miles (2.4)' },
  { group: 'Aircraft', name: 'planedir', label: 'Direction to look (NE)' },
  { group: 'Aircraft', name: 'planehdg', label: 'Direction it flies (SE)' },
  { group: 'Aircraft', name: 'planes', label: 'Aircraft in range (7)' },
];

export const SAMPLES = {
  planeline: 'UAL123 SFO-ORD B738', planewhere: 'LOOK NE 2.4 NM 10800 FT', planetrip: 'TO CHICAGO',
  plane: 'UAL123', planeroute: 'SFO-ORD', planefrom: 'SFO', planeto: 'ORD', planetype: 'B738',
  planealt: '10800', planespd: '314', planedist: '2.4', planedir: 'NE', planehdg: 'SE', planes: '7',
};

// What the feed shows with nothing in range (the rest are "--").
export const NONE = { planeline: 'NO PLANES', planewhere: 'NONE WITHIN 10 NM', planetrip: 'QUIET SKIES', planes: '0' };

// The longest value of each the feed can produce (the C host test checks the
// same limits): an 11-character registration, ICAO airport codes, a city cut
// to 20 characters, six-digit altitudes.
export const WORST = {
  planeline: 'ABCDEFGHIJK KSFO-KORD B77W', planewhere: 'LOOK NW 25.9 NM 100000 FT', planetrip: `FROM ${'M'.repeat(20)}`,
  plane: 'ABCDEFGHIJK', planeroute: 'KSFO-KORD', planefrom: 'KSFO', planeto: 'KORD', planetype: 'B77W',
  planealt: '100000', planespd: '1999', planedist: '25.9', planedir: 'NW', planehdg: 'NW', planes: '999',
};

// The scene the overhead alert plays ("plane"); also fine in the playlist.
// The flight flies in from the right and keeps going round, the trip bounces
// up, then where to look.
export const PLANE_SCENE = {
  name: 'Plane overhead',
  frames: [
    { text: '{var:planeline}', entrance: 1, after: 'spin', blink: 0, shift: -1, seconds: 12 },
    { text: '{var:planetrip}', entrance: 24, after: 'spin', blink: 0, shift: -1, seconds: 7 },
    { text: '{var:planewhere}', entrance: 26, after: 'spin', blink: 0, shift: -1, seconds: 10 },
  ],
};
