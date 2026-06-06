#include "Animation.h"

Animation::Animation(SpriteSheet* spriteSheet, float frameTime, bool loop)
    : spriteSheet(spriteSheet), frameTime(frameTime), loop(loop),
      timer(0.0f), currentFrame(0), playing(true), finished(false) {
}

void Animation::AddFrame(int index) {
    frames.push_back(index);
}

void Animation::AddFrames(int startIndex, int count) {
    for (int i = 0; i < count; i++) {
        frames.push_back(startIndex + i);
    }
}

void Animation::AddFrameRange(int startIndex, int endIndex) {
    for (int i = startIndex; i <= endIndex; i++) {
        frames.push_back(i);
    }
}

void Animation::Update(float deltaTime) {
    if (!playing || frames.empty() || finished) return;

    timer += deltaTime;
    if (timer >= frameTime) {
        timer -= frameTime;
        currentFrame++;

        if (currentFrame >= static_cast<int>(frames.size())) {
            if (loop) {
                currentFrame = 0;
            } else {
                currentFrame = static_cast<int>(frames.size()) - 1;
                finished = true;
                playing = false;
            }
        }
    }
}

void Animation::Play() {
    playing = true;
    if (finished) {
        Reset();
    }
}

void Animation::Pause() {
    playing = false;
}

void Animation::Stop() {
    playing = false;
    Reset();
}

void Animation::Reset() {
    currentFrame = 0;
    timer = 0.0f;
    finished = false;
}

Frame Animation::GetCurrentFrame() const {
    if (frames.empty()) {
        return spriteSheet->GetFrame(0);
    }
    return spriteSheet->GetFrame(frames[currentFrame]);
}
