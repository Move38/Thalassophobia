/*
 *  Thalassophobia
 *  by Gabriel Jones 2020
 *  Lead development by Gabriel Jones
 *  Additional development by Daniel King
 *
 *  --------------------
 *  Blinks by Move38
 *  Brought to life via Kickstarter 2018
 *
 *  @madewithblinks
 *  www.move38.com
 *  --------------------
 */

#define PATH_COLOR BLUE
#define AVATAR_COLOR GREEN
#define WALL_COLOR RED
#define FOG_COLOR dim(WHITE, 32)
#define RESET_COLOR MAGENTA
#define STAIRS_COLOR YELLOW
#define REVERT_TIME_PATH 2000
#define REVERT_TIME_WALL  2000
#define STAIR_INTERVAL    8000
//#define GAME_TIME_MAX 180000 //3 minutes
#define GAME_TIME_MAX 360000 //6 minutes
//#define GAME_TIME_MAX 10000 //10 seconds
enum state {INIT, AVATAR, AVATAR_ENTERING, AVATAR_LEAVING, AVATAR_ASCENDED, FOG, PATH, WALL, GAME_OVER, BROADCAST, BROADCAST_IGNORE};

//              0     1      10   11    100   101       110             111      1000      1001      1010      1011      1100      1101      1110
enum protoc {NONE, MOVE, ASCEND, WIN, RESET, DEPARTED, UNUSED_2, LEVEL_MASK, AVATAR_0, AVATAR_1, AVATAR_2, AVATAR_3, AVATAR_4, AVATAR_5, AVATAR_6};
Timer timer;
Timer stairsTimer;
unsigned long startMillis;
bool isStairs;
bool won = false;
byte heading = 255;
protoc broadcastMessage = NONE;
protoc level = AVATAR_6;
state postBroadcastState;
state state;

// should always be last call of loopState_ function
void handleBroadcasts(bool handleResetRequest, bool ignoreAscend) {
  if (handleResetRequest && buttonLongPressed()) {
    broadcastMessage = RESET;
    enterState_Broadcast(); return;
  }
  broadcastMessage = NONE;
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      protoc lastValue = getLastValueReceivedOnFace(f);
      switch (lastValue) {
        case ASCEND:
          if (ignoreAscend) break;
        case WIN:
        case RESET:
          broadcastMessage = lastValue;
          break;
      }
    }
  }
  if (broadcastMessage != NONE) {
    enterState_Broadcast();
    return;
  }
}

void setColorHalfHeading(Color color) {
  setColorOnFace(color, heading);
  setColorOnFace(color, (heading + 1) % 6);
  setColorOnFace(color, (heading + 5) % 6);
}

void pointHeadingToAdjacentAvatar() {
  heading = 255;
  FOREACH_FACE(f) { //check if avatar is on neighbor
    if (!isValueReceivedOnFaceExpired(f)) {
      protoc lastValue = getLastValueReceivedOnFace(f);
      if ((lastValue & AVATAR_0) == AVATAR_0) { // is avatar?
        heading = f; //update heading
        level = lastValue; //update my level to match that of avatar, for animating the current level on the blinks around the avatar
        break;
      }
    }
  }
}

bool isAvatarAdjacent() {
  return heading < FACE_COUNT;
}

bool handleGameTimer() {
  if (millis() - startMillis > GAME_TIME_MAX) {
    enterState_GameOver();
    return true;
  } else {
    return false;
  }
}

void moveStairs() {
  if (stairsTimer.isExpired()) {
    isStairs = random(30) == 0;
    stairsTimer.set(STAIR_INTERVAL);
  }
}

Color dimToLevel(Color color) {
  return color;
  byte level_inv = AVATAR_6 - level;
  byte bright = map(level_inv, 0, AVATAR_5 & LEVEL_MASK, 32, MAX_BRIGHTNESS);
  return dim(color, bright);
}

void enterState_Avatar() {
  setValueSentOnAllFaces(level);
  setColor(OFF);
  state = AVATAR;
}

void loopState_Avatar() {
  FOREACH_FACE(f) {
    if (!isValueReceivedOnFaceExpired(f)) {
      switch (getLastValueReceivedOnFace(f)) {
        case MOVE: //avatar is being pulled to neighbor
          heading = f;
          enterState_AvatarLeaving(); return;
          break;
      }
    }
  }

  buttonSingleClicked(); //do nothing just consume errant click

  avatarDisplay();

  if (handleGameTimer()) return;
  handleBroadcasts(true, true);
}

void enterState_AvatarLeaving() {
  setValueSentOnAllFaces(NONE);
  setValueSentOnFace(DEPARTED, heading);
  setColor(dimToLevel(PATH_COLOR));
  setColorHalfHeading(dim(WHITE, 100));
  state = AVATAR_LEAVING;
}

void loopState_AvatarLeaving() {
  if (!isValueReceivedOnFaceExpired(heading)) {
    // if neighbor is sending avatar then the avatar has successfully moved
    if ((getLastValueReceivedOnFace(heading) & AVATAR_0) == AVATAR_0) {
      setColor(dimToLevel(PATH_COLOR));
      enterState_Path(); return;
    }
  }

  if (handleGameTimer()) return;
  handleBroadcasts(true, false);
}

void enterState_AvatarEntering() {
  setValueSentOnFace(MOVE, heading); //request the avatar move here
  setColor(dimToLevel(PATH_COLOR));
  setColorHalfHeading(dim(WHITE, 100));
  state = AVATAR_ENTERING;
}

void loopState_AvatarEntering() {
  if (!isValueReceivedOnFaceExpired(heading)) {
    switch (getLastValueReceivedOnFace(heading)) {
      case DEPARTED: //former avatar blink is acknowledging move request
        if (isStairs) {
          enterState_AvatarAscended(); return;
        } else {
          enterState_Avatar(); return;
        }
      case NONE: //former avatar tile became path, avatar must have moved to some other blink
        enterState_Path(); return; //revert back to path
      default:
        break;
    }
  }

  if (handleGameTimer()) return;
  handleBroadcasts(true, true);
}

void enterState_AvatarAscended() {
  timer.set(750);
  if (level <= AVATAR_0) {//we won
    won = true;
    broadcastMessage = WIN;
  } else {
    broadcastMessage = ASCEND;
  }
  setColor(OFF);
  setColorOnFace(AVATAR_COLOR, 0);
  setValueSentOnAllFaces(broadcastMessage);
  state = AVATAR_ASCENDED;
}

void loopState_AvatarAscended() {
  if (timer.isExpired()) {
    isStairs = false;
    level = level - 1;
    enterState_Avatar(); return;
  }
}

void enterState_Fog() {

  setValueSentOnAllFaces(NONE);
  fogDisplay();

  state = FOG;
}

void loopState_Fog() {
  pointHeadingToAdjacentAvatar();

  if (isAvatarAdjacent()) {
    byte chance = random(20);
    if (chance < 11) {
      enterState_Path();
      return;
    } else {
      enterState_Wall();
      return;
    }
  } else {
    moveStairs();
  }

  buttonSingleClicked(); //do nothing just consume errant click
  if (buttonLongPressed()) {
    enterState_Avatar();
    return;
  }

  if (handleGameTimer()) return;
  handleBroadcasts(false, false);
}

void enterState_Path() {
  setValueSentOnAllFaces(NONE);
  timer.set(REVERT_TIME_PATH); //revert to fog after a bit
  state = PATH;
}

void loopState_Path() {
  if(isAlone()) {
    enterState_Fog();
    return;
  }

  pointHeadingToAdjacentAvatar();

  if (timer.isExpired()) {
    if (!isAvatarAdjacent()) {
      enterState_Fog();
      return;
    }
  }

  if (buttonSingleClicked()) {
    if (isAvatarAdjacent()) {
      enterState_AvatarEntering(); return;
    }
  }

  if (isAvatarAdjacent()) {
    timer.set(REVERT_TIME_PATH);
  } else {
    moveStairs();
  }

  pathDisplay();

  if (handleGameTimer()) return;
  handleBroadcasts(true, false);
}

void enterState_Wall() {
  setValueSentOnAllFaces(NONE);
  timer.set(REVERT_TIME_WALL); //revert to fog after a bit
  state = WALL;
}

void loopState_Wall() {
  if (isAlone()) {
    enterState_Fog();
    return;
  }

  pointHeadingToAdjacentAvatar();

  if (timer.isExpired()) {
    if (!isAvatarAdjacent()) {
      enterState_Fog();
      return;
    }
  }

  if (buttonSingleClicked()) {
    if (isAvatarAdjacent() && isStairs) {
      enterState_AvatarEntering();
      return;
    }
  }

  if (isAvatarAdjacent()) {
    timer.set(REVERT_TIME_WALL);
  } else {
    moveStairs();
  }

  wallDisplay();

  if (handleGameTimer()) return;
  handleBroadcasts(true, false);
}

void enterState_GameOver() {
  setValueSentOnAllFaces(NONE);
  if (won) { //TODO better win celebration animation
    FOREACH_FACE(f) {
      setColorOnFace(dim(STAIRS_COLOR, f * (255 / 6)), f);
    }
  } else {
    setColor(WALL_COLOR);
    FOREACH_FACE(f) {
      if (f % 2 == 0) setColorOnFace(OFF, f);
    }
  }
  state = GAME_OVER;
}

void loopState_GameOver() {

  //animate

  byte offset = (millis() % 1200 / 200);
  if (won) {
    FOREACH_FACE(f) {
      setColorOnFace(dim(STAIRS_COLOR, (f - offset) % 6 * (255 / 6)), f);
    }
  }

  handleBroadcasts(true, true);
}

void enterState_Broadcast() {
  timer.set(500);
  setValueSentOnAllFaces(broadcastMessage);
  switch (broadcastMessage) {
    case ASCEND:
      fogDisplay();
      isStairs = false;
      stairsTimer.set(STAIR_INTERVAL); //prevent stairs from popping next to avatar immediately on ascension
      postBroadcastState = FOG;
      break;
    case WIN:
      won = true;
      setColor(WHITE);
      postBroadcastState = GAME_OVER;
      break;
    case RESET:
      fogDisplay();
      postBroadcastState = INIT;
      break;
  }
  state = BROADCAST;
}

void loopState_Broadcast() {
  if (timer.isExpired()) {
    enterState_BroadcastIgnore();
    return;
  }
}

void enterState_BroadcastIgnore() {
  timer.set(500);
  setValueSentOnAllFaces(NONE); //stop broadcasting
  fogDisplay();
  state = BROADCAST_IGNORE;
}

void loopState_BroadcastIgnore() {
  if(timer.isExpired()) { //stop ignoring
    state = postBroadcastState;

    switch (postBroadcastState) {
      case INIT:
        enterState_Init();
        break;
//        case AVATAR:
//          enterState_Avatar();
//          break;
//        case AVATAR_ENTERING:
//          enterState_AvatarEntering();
//          break;
//        case AVATAR_LEAVING:
//          enterState_AvatarLeaving();
//          break;
//        case AVATAR_ASCENDED:
//          enterState_AvatarAscended();
//          break;
      case FOG:
        enterState_Fog();
        break;
//        case PATH:
//          enterState_Path();
//          break;
//        case WALL:
//          enterState_Wall();
//          break;
      case GAME_OVER:
        enterState_GameOver();
        break;
//        case BROADCAST:
//          enterState_Broadcast();
//          break;
//        case BROADCAST_IGNORE:
//          enterState_BroadcastIgnore();
//          break;
     }
  }
}

void enterState_Init() {
  setValueSentOnAllFaces(NONE);
  startMillis = millis();
  randomize();
  won = false;
  level = AVATAR_6;
  broadcastMessage = NONE;
  //setColor(dimToLevel(GREEN));
  fogDisplay();
  enterState_Fog(); return; //shortcircuit straight to FOG
}

void setup() {
  enterState_Init();
}

void loop() {
  switch (state) {
    case INIT:
      //should never be in this state
      break;
    case AVATAR:
      loopState_Avatar();
      break;
    case AVATAR_ENTERING:
      loopState_AvatarEntering();
      break;
    case AVATAR_LEAVING:
      loopState_AvatarLeaving();
      break;
    case AVATAR_ASCENDED:
      loopState_AvatarAscended();
      break;
    case FOG:
      loopState_Fog();
      break;
    case PATH:
      loopState_Path();
      break;
    case WALL:
      loopState_Wall();
      break;
    case GAME_OVER:
      loopState_GameOver();
      break;
    case BROADCAST:
      loopState_Broadcast();
      break;
    case BROADCAST_IGNORE:
      loopState_BroadcastIgnore();
      break;
  }
}

//special new display nonsense
#define WATER_HUE_DEEP 180
#define WATER_HUE_SHALLOW 110

#define GRASS_HUE_DEEP 70
#define GRASS_HUE_SHALLOW 50

#define WATER_SAT_DEEP 255
#define WATER_SAT_SHALLOW 200

#define WATER_BRI_DEEP 150
#define WATER_BRI_SHALLOW 255

#define BREATH_RATE_MIN 1000
#define BREATH_RATE_MAX 4000

void avatarDisplay() {

  //animate time remaining
  //  byte blinkFace = (millis() - startMillis) / (GAME_TIME_MAX / 6);
  //  Color  on = AVATAR_COLOR;
  //  Color off = dim(AVATAR_COLOR,  32);
  //  Color blinq = millis() % 1000 / 500 == 0 ? off : on;

  setColor(dim(WHITE, 75));
  byte airLevel = (GAME_TIME_MAX + startMillis - millis()) / (GAME_TIME_MAX / 6); //goes from 6 - 1 over time (and I guess 0 eventually)

  //byte breathBrightness = map(sin8_C(map(millis() % map(GAME_TIME_MAX + startMillis - millis(), 0, GAME_TIME_MAX, BREATH_RATE_MIN, BREATH_RATE_MAX), 0, map(GAME_TIME_MAX + startMillis - millis(), 0, GAME_TIME_MAX, BREATH_RATE_MIN, BREATH_RATE_MAX), 0, 255)), 0, 255, 100, 255);
  //byte breathBrightness = map(sin8_C(), 0, 255, 100, 255);
  byte breathBrightness = map(sin8_C(map(millis() % map(airLevel, 0, 6, BREATH_RATE_MIN, BREATH_RATE_MAX), 0, map(airLevel, 0, 6, BREATH_RATE_MIN, BREATH_RATE_MAX), 0, 255)), 0, 255, 100, 255);


  FOREACH_FACE(f) {
    if (f <= airLevel) {
      setColorOnFace(dim(WHITE, breathBrightness), f);
    }
  }
}

void pathDisplay() {

  byte currentHue = map(level, 0, AVATAR_5 & LEVEL_MASK, WATER_HUE_SHALLOW, WATER_HUE_DEEP);
  byte currentSat = map(level, 0, AVATAR_5 & LEVEL_MASK, WATER_SAT_SHALLOW, WATER_SAT_DEEP);
  byte currentBri = map(timer.getRemaining(), 0, REVERT_TIME_PATH, 60, map(level, 0, AVATAR_5 & LEVEL_MASK, WATER_BRI_DEEP, WATER_BRI_SHALLOW));

  setColor(makeColorHSB(currentHue, currentSat, currentBri));

  if (isStairs) {
    stairDisplay(currentHue, currentSat, currentBri);

    //      setColorOnFace(makeColorHSB(currentHue, 0, currentBri), random(5));
    //      setColorOnFace(makeColorHSB(currentHue, 0, currentBri), random(5));
  }

}

void wallDisplay() {
  byte grassHue = map(level, 0, AVATAR_5 & LEVEL_MASK, GRASS_HUE_SHALLOW, GRASS_HUE_DEEP);
  byte waterHue = map(level, 0, AVATAR_5 & LEVEL_MASK, WATER_HUE_SHALLOW, WATER_HUE_DEEP);
  byte currentHue = map(REVERT_TIME_PATH - timer.getRemaining(), 0, REVERT_TIME_PATH, grassHue, waterHue);
  byte currentBri = map(timer.getRemaining(), 0, REVERT_TIME_PATH, 60, map(level, 0, AVATAR_5 & LEVEL_MASK, WATER_BRI_DEEP, WATER_BRI_SHALLOW));

  setColor(makeColorHSB(currentHue, 255, currentBri));

  if (isStairs) {
    stairDisplay(currentHue, 255, currentBri);
    //      setColorOnFace(makeColorHSB(currentHue, 0, currentBri), random(5));
    //      setColorOnFace(makeColorHSB(currentHue, 0, currentBri), random(5));
  }
}

#define SPARKLE_CYCLE_TIME 1000
#define SPARKLE_FLASH_TIME 100
byte sparkleOffset[6] = {0, 3, 5, 1, 4, 2};

void stairDisplay(byte hue, byte sat, byte bri) {
  byte sparkleFrame = (millis() % SPARKLE_CYCLE_TIME) / SPARKLE_FLASH_TIME;
  if (sparkleFrame < 6) {
    setColorOnFace(dim(WHITE, bri), sparkleOffset[sparkleFrame]);
  }
}

void fogDisplay() {
  byte currentHue = map(level, 0, AVATAR_5 & LEVEL_MASK, WATER_HUE_SHALLOW, WATER_HUE_DEEP);
  byte currentSat = map(level, 0, AVATAR_5 & LEVEL_MASK, WATER_SAT_SHALLOW, WATER_SAT_DEEP);
  FOREACH_FACE(f) {
    setColorOnFace(makeColorHSB(currentHue, currentSat, 60 - random(40)), f);
  }
}
