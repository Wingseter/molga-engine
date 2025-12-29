#ifndef MOLGA_ANIMATION_H
#define MOLGA_ANIMATION_H

#include "SpriteSheet.h"
#include <vector>

class Animation {
public:
    Animation(SpriteSheet* spriteSheet, float frameTime = 0.1f, bool loop = true);

    void AddFrame(int index);
    void AddFrames(int startIndex, int count);
    void AddFrameRange(int startIndex, int endIndex);

    void Update(float deltaTime);
    void Play();
    void Pause();
    void Stop();
    void Reset();

    Frame GetCurrentFrame() const;
    int GetCurrentFrameIndex() const { return currentFrame; }
    bool IsPlaying() const { return playing; }
    bool IsFinished() const { return finished; }

    void SetFrameTime(float time) { frameTime = time; }
    void SetLoop(bool loop) { this->loop = loop; }
    SpriteSheet* GetSpriteSheet() const { return spriteSheet; }

private:
    SpriteSheet* spriteSheet;
    std::vector<int> frames;
    float frameTime;
    float timer;
    int currentFrame;
    bool playing;
    bool loop;
    bool finished;
};

#endif // MOLGA_ANIMATION_H
