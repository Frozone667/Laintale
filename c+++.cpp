// main.cpp  (FULL FIXED VERSION)
// - SFML 3 sprite construction (no default sf::Sprite)
// - Heart-shaped soul (Option 2) using sf::ConvexShape
// - When pressing Attack: heart appears at player's position and flies to battle box center
// - Keeps overworld player sprite separate from battle soul

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <SFML/Config.hpp>

#include <vector>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <string>

using namespace std;

static float clampf(float v, float lo, float hi) { return max(lo, min(hi, v)); }
static float leftOf(const sf::FloatRect& r) { return r.position.x; }
static float topOf(const sf::FloatRect& r) { return r.position.y; }
static float rightOf(const sf::FloatRect& r) { return r.position.x + r.size.x; }
static float bottomOf(const sf::FloatRect& r) { return r.position.y + r.size.y; }

enum class GameMode {
    Overworld,
    EncounterMenu,
    SoulFlyIn,      // NEW: heart appears at player pos, flies to battle center
    Battle,
    AttackTurn,
    DamageMsg,
    EnemyDefeated,
    Victory,
    GameOver
};

struct Bullet {
    sf::Vector2f pos;
    sf::Vector2f vel;
    float r = 6.f;
    bool alive = true;
    void update(float dt) { pos += vel * dt; }
};

struct PlayerOverworld {
    sf::Vector2f pos{ 120.f, 260.f };
    sf::Vector2f size{ 28.f, 28.f }; // collision hitbox size (gameplay)
    float speed = 220.f;
};

struct Soul {
    sf::Vector2f pos{ 0.f, 0.f };     // top-left of soul hitbox
    sf::Vector2f size{ 14.f, 14.f };  // soul hitbox size
    float speed = 260.f;

    int hp = 20;
    int maxHp = 20;

    bool invuln = false;
    float invulnTimer = 0.f;
};

struct Encounter {
    sf::FloatRect trigger;
    bool active = true;
};

static bool intersects(const sf::FloatRect& a, const sf::FloatRect& b) {
    return a.findIntersection(b).has_value();
}

// Edge-trigger helper: returns true only on the frame the key becomes pressed.
static bool justPressed(sf::Keyboard::Key key, bool& prev) {
    bool now = sf::Keyboard::isKeyPressed(key);
    bool pressed = (now && !prev);
    prev = now;
    return pressed;
}

enum class Dir { Up = 0, Down = 1, Left = 2, Right = 3 };

struct WalkAnim {
    Dir dir = Dir::Down;
    int frame = 0;          // 0..3
    float timer = 0.f;
    float frameTime = 0.10f;
    bool moving = false;
};

static bool loadFrames(vector<sf::Texture>& out, const vector<string>& files) {
    out.clear();
    out.resize(files.size());
    for (size_t i = 0; i < files.size(); ++i) {
        if (!out[i].loadFromFile(files[i])) {
            cerr << "ERROR: couldn't load player frame: " << files[i] << "\n";
            return false;
        }
        out[i].setSmooth(false);
    }
    return true;
}

int main() {
    srand((unsigned)time(nullptr));

    const unsigned W = 900;
    const unsigned H = 520;

    sf::RenderWindow window(sf::VideoMode({ W, H }), "Overworld + Battle Turns (SFML)");
    window.setFramerateLimit(60);

    // ===============================
    // SFX (WAV/OGG recommended for sf::Sound; MP3 is NOT supported by sf::Sound)
    // ===============================
    sf::SoundBuffer hpDownBuf;
    if (!hpDownBuf.loadFromFile("assets/sfx_hpdown.wav")) {
        std::cerr << "ERROR: couldn't load assets/sfx_hpdown.wav\n";
    }
    sf::Sound hpDownSfx(hpDownBuf);
    hpDownSfx.setVolume(70.f);

    // -----------------------------
    // GAME STATE
    // -----------------------------
    GameMode mode = GameMode::Overworld;
    GameMode lastMode = mode;
    bool firstMusic = true;

    PlayerOverworld p;
    Encounter encounter{ sf::FloatRect({640.f, 250.f}, {80.f, 80.f}), true };

    // Overworld walls
    vector<sf::FloatRect> walls;
    const float t = 30.f;
    walls.push_back(sf::FloatRect({ 0.f, 0.f }, { (float)W, t }));
    walls.push_back(sf::FloatRect({ 0.f, (float)H - t }, { (float)W, t }));
    walls.push_back(sf::FloatRect({ 0.f, 0.f }, { t, (float)H }));
    walls.push_back(sf::FloatRect({ (float)W - t, 0.f }, { t, (float)H }));
    walls.push_back(sf::FloatRect({ 360.f, 180.f }, { 160.f, 40.f }));

    // Battle box
    sf::FloatRect battleBox({ 260.f, 140.f }, { 380.f, 240.f });

    Soul soul;
    // default soul position (top-left) to center of battle box
    soul.pos = {
        leftOf(battleBox) + battleBox.size.x / 2.f - soul.size.x / 2.f,
        topOf(battleBox) + battleBox.size.y / 2.f - soul.size.y / 2.f
    };

    vector<Bullet> bullets;
    float spawnTimer = 0.f;
    float battleTime = 0.f;

    float defeatTimer = 0.f;
    int lastDamage = 0;
    float msgTimer = 0.f;

    // Enemy stats
    int enemyMaxHp = 100;
    int enemyHp = enemyMaxHp;

    // Animated HP ONLY for DamageMsg screen
    float enemyHpShown = (float)enemyHp;
    float enemyHpFrom = (float)enemyHp;
    float enemyHpTo = (float)enemyHp;
    float hpAnimT = 0.f;
    float hpAnimDur = 1.7f;

    int battleStage = 1; // 1 = first defense, 2 = second defense
    int menuIndex = 0;   // 0 walk away, 1 attack
    bool playedHpDownSfx = false;

    // -----------------------------
    // NEW: Soul fly-in transition variables
    // -----------------------------
    sf::Vector2f soulFlyStart;
    sf::Vector2f soulFlyTarget;
    float soulFlyT = 0.f;
    float soulFlyDur = 1.5f; // seconds

    // -----------------------------
    // MUSIC (ONE INSTANCE ONLY)
    // -----------------------------
    sf::Music music;
    std::string currentTrack;

    auto playMusic = [&](const std::string& file, bool loop = true, float volume = 55.f) {
        if (currentTrack == file && music.getStatus() == sf::SoundSource::Status::Playing)
            return;

        music.stop();

        if (!music.openFromFile(file)) {
            std::cerr << "ERROR loading music: " << file << "\n";
            currentTrack.clear();
            return;
        }

        currentTrack = file;

#if SFML_VERSION_MAJOR >= 3
        music.setLooping(loop);
#else
        music.setLoop(loop);
#endif
        music.setVolume(volume);
        music.play();
        };

    // Start music immediately
    playMusic("assets/music/menu.mp3", true, 55.f);

    // -----------------------------
    // LOAD ENEMY SPRITE
    // -----------------------------
    sf::Texture enemyTex;
    if (!enemyTex.loadFromFile("assets/enemy.jpeg")) {
        std::cerr << "ERROR: couldn't load assets/enemy.jpeg\n";
        return 1;
    }
    sf::Sprite enemySprite(enemyTex);
    enemySprite.setScale({ 0.25f, 0.25f });

    // set origin ONCE using local bounds (stable)
    sf::FloatRect lb = enemySprite.getLocalBounds();
    enemySprite.setOrigin({ lb.position.x + lb.size.x / 2.f, lb.position.y + lb.size.y / 2.f });


    // -----------------------------
    // FONT
    // -----------------------------
    sf::Font font;
    bool hasFont = font.openFromFile("assets/font.ttf");

    sf::Text menuTitle(font), optionWalk(font), optionAttack(font), hintText(font);
    sf::Text victoryTitle(font), victoryHint(font);

    if (hasFont) {
        menuTitle.setCharacterSize(22);
        optionWalk.setCharacterSize(18);
        optionAttack.setCharacterSize(18);
        hintText.setCharacterSize(10);
        hintText.setLineSpacing(1.5f);

        menuTitle.setFillColor(sf::Color::White);
        optionWalk.setFillColor(sf::Color::White);
        optionAttack.setFillColor(sf::Color::White);
        hintText.setFillColor(sf::Color(200, 200, 200));

        menuTitle.setString("Enemy Encounter");
        optionWalk.setString("Walk away");
        optionAttack.setString("Attack");
        hintText.setString("Use W/S to choose, \n"
            "Enter to confirm, Esc to cancel");

        victoryTitle.setCharacterSize(48);
        victoryTitle.setFillColor(sf::Color::Yellow);
        victoryTitle.setStyle(sf::Text::Bold);
        victoryTitle.setString("YOU WON!");

        victoryHint.setCharacterSize(20);
        victoryHint.setFillColor(sf::Color(200, 200, 200));
        victoryHint.setString("Press Enter to continue");
    }

    // -----------------------------
    // PLAYER ANIMATED SPRITE (4 dirs x 4 frames)
    // -----------------------------
    vector<sf::Texture> framesUp, framesDown, framesLeft, framesRight;

    if (!loadFrames(framesUp, { "assets/player/W1.png","assets/player/W2.png","assets/player/W3.png","assets/player/W4.png" })) return 1;
    if (!loadFrames(framesDown, { "assets/player/D1.png","assets/player/D2.png","assets/player/D3.png","assets/player/D4.png" })) return 1;
    if (!loadFrames(framesLeft, { "assets/player/L1.png","assets/player/L2.png","assets/player/L3.png","assets/player/L4.png" })) return 1;
    if (!loadFrames(framesRight, { "assets/player/R1.png","assets/player/R2.png","assets/player/R3.png","assets/player/R4.png" })) return 1;

    // SFML 3: must construct sprite with a texture
    sf::Sprite playerSprite(framesDown[0]);

    // Scale sprite relative to hitbox, but keep a VISUAL multiplier so it isn't tiny
    auto texSize0 = framesDown[0].getSize();
    float visualScale = 1.8f; // tweak 1.5f..2.3f
    playerSprite.setScale({
        (p.size.x / (float)texSize0.x) * visualScale,
        (p.size.y / (float)texSize0.y) * visualScale
        });
    playerSprite.setOrigin({ texSize0.x / 2.f, texSize0.y / 2.f });

    WalkAnim walk;

    auto setPlayerFrame = [&]() {
        int f = std::clamp(walk.frame, 0, 3);
        switch (walk.dir) {
        case Dir::Up:    playerSprite.setTexture(framesUp[f], true); break;
        case Dir::Down:  playerSprite.setTexture(framesDown[f], true); break;
        case Dir::Left:  playerSprite.setTexture(framesLeft[f], true); break;
        case Dir::Right: playerSprite.setTexture(framesRight[f], true); break;
        }
        };

    // -----------------------------
    // RENDERING SHAPES
    // -----------------------------
    sf::RectangleShape roomBg(sf::Vector2f((float)W, (float)H));
    roomBg.setFillColor(sf::Color(20, 22, 26));

    sf::RectangleShape wallShape;
    wallShape.setFillColor(sf::Color(70, 70, 80));

    sf::RectangleShape triggerOutline;
    triggerOutline.setFillColor(sf::Color::Transparent);
    triggerOutline.setOutlineThickness(2.f);
    triggerOutline.setOutlineColor(sf::Color(220, 160, 30));

    sf::RectangleShape boxShape(sf::Vector2f(battleBox.size.x, battleBox.size.y));
    boxShape.setFillColor(sf::Color::Transparent);
    boxShape.setOutlineThickness(4.f);
    boxShape.setOutlineColor(sf::Color::White);
    boxShape.setPosition(battleBox.position);

    // -----------------------------
    // SOUL HEART SHAPE (Option 2)
    // -----------------------------
    sf::ConvexShape soulShape;
    soulShape.setPointCount(8);
    soulShape.setPoint(0, { 8.f, 0.f });
    soulShape.setPoint(1, { 16.f, 4.f });
    soulShape.setPoint(2, { 24.f, 0.f });
    soulShape.setPoint(3, { 32.f, 10.f });
    soulShape.setPoint(4, { 16.f, 28.f });
    soulShape.setPoint(5, { 0.f, 10.f });
    soulShape.setPoint(6, { 8.f, 0.f });
    soulShape.setPoint(7, { 16.f, 12.f });
    soulShape.setFillColor(sf::Color::Red);

    sf::FloatRect heartBounds = soulShape.getLocalBounds();
    soulShape.setScale({
        soul.size.x / heartBounds.size.x,
        soul.size.y / heartBounds.size.y
        });
    soulShape.setOrigin({
        heartBounds.size.x / 2.f,
        heartBounds.size.y / 2.f
        });

    sf::RectangleShape hpBack(sf::Vector2f(240.f, 16.f));
    hpBack.setFillColor(sf::Color(60, 60, 60));
    hpBack.setPosition(sf::Vector2f{ leftOf(battleBox), bottomOf(battleBox) + 18.f });

    sf::RectangleShape hpFill(sf::Vector2f(240.f, 16.f));
    hpFill.setFillColor(sf::Color(60, 220, 80));
    hpFill.setPosition(hpBack.getPosition());

    sf::RectangleShape menuPanel(sf::Vector2f(420.f, 220.f));
    menuPanel.setFillColor(sf::Color(0, 0, 0, 190));
    menuPanel.setOutlineThickness(3.f);
    menuPanel.setOutlineColor(sf::Color::White);
    menuPanel.setPosition(sf::Vector2f{ 240.f, 150.f });

    sf::RectangleShape selector(sf::Vector2f(12.f, 12.f));
    selector.setFillColor(sf::Color(255, 255, 0));

    sf::RectangleShape enemyHpBack(sf::Vector2f(260.f, 12.f));
    enemyHpBack.setFillColor(sf::Color(60, 60, 60));

    sf::RectangleShape enemyHpFill(sf::Vector2f(260.f, 12.f));
    enemyHpFill.setFillColor(sf::Color(220, 80, 80));

    // -----------------------------
    // Input edge states
    // -----------------------------
    bool prevE = false;
    bool prevEnter = false;
    bool prevEsc = false;
    bool prevW = false;
    bool prevS = false;

    sf::Clock clock;

    auto startBattlePhase = [&]() {
        mode = GameMode::Battle;

        bullets.clear();
        spawnTimer = 0.f;
        battleTime = 0.f;

        soul.invuln = false;
        soul.invulnTimer = 0.f;

        // Keep soul where it currently is (end of fly-in) or set to center if you want hard reset:
        // soul.pos = {
        //     leftOf(battleBox) + battleBox.size.x / 2.f - soul.size.x / 2.f,
        //     topOf(battleBox) + battleBox.size.y / 2.f - soul.size.y / 2.f
        // };
        };

    auto startSoulFlyIn = [&]() {
        mode = GameMode::SoulFlyIn;
        soulFlyT = 0.f;

        // spawn at player's current overworld position (convert to soul top-left)
        sf::Vector2f playerCenter = { p.pos.x + p.size.x / 2.f, p.pos.y + p.size.y / 2.f };
        soulFlyStart = { playerCenter.x - soul.size.x / 2.f, playerCenter.y - soul.size.y / 2.f };

        // target = battle box center (soul top-left)
        sf::Vector2f boxCenter = {
            leftOf(battleBox) + battleBox.size.x / 2.f,
            topOf(battleBox) + battleBox.size.y / 2.f
        };
        soulFlyTarget = { boxCenter.x - soul.size.x / 2.f, boxCenter.y - soul.size.y / 2.f };

        soul.pos = soulFlyStart;

        // prep soul status
        soul.invuln = false;
        soul.invulnTimer = 0.f;

        bullets.clear();
        spawnTimer = 0.f;
        battleTime = 0.f;
        };

    auto drawEnemyAtTrigger = [&]() {
        float ex = encounter.trigger.position.x + encounter.trigger.size.x / 2.f;
        float ey = encounter.trigger.position.y + encounter.trigger.size.y / 2.f;

        enemySprite.setPosition({ ex, ey });
        window.draw(enemySprite);
        };


    while (window.isOpen()) {
        while (auto ev = window.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) window.close();
        }

        float dt = clock.restart().asSeconds();
        dt = min(dt, 0.05f);

        // -----------------------------
        // MUSIC SWITCH ON MODE CHANGE
        // -----------------------------
        if (firstMusic || mode != lastMode) {
            switch (mode) {
            case GameMode::Overworld:
                playMusic("assets/music/menu.mp3", true, 55.f);
                break;
            case GameMode::EncounterMenu:
                playMusic("assets/music/interaction.mp3", true, 55.f);
                break;
            case GameMode::SoulFlyIn:
            case GameMode::Battle:
            case GameMode::AttackTurn:
            case GameMode::DamageMsg:
            case GameMode::EnemyDefeated:
                playMusic("assets/music/battle.mp3", true, 60.f);
                break;
            case GameMode::Victory:
                playMusic("assets/music/victory.mp3", false, 70.f);
                break;
            case GameMode::GameOver:
                playMusic("assets/music/gameover.mp3", true, 55.f);
                break;
            default:
                break;
            }
            lastMode = mode;
            firstMusic = false;
        }

        // -----------------------------
        // UPDATE
        // -----------------------------
        if (mode == GameMode::Overworld) {
            sf::Vector2f move(0.f, 0.f);
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) move.y -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) move.y += 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) move.x -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) move.x += 1.f;

            if (move.x != 0.f || move.y != 0.f) {
                float len = sqrt(move.x * move.x + move.y * move.y);
                move /= len;
            }

            // direction for animation
            walk.moving = (move.x != 0.f || move.y != 0.f);
            if (walk.moving) {
                if (fabs(move.x) > fabs(move.y)) {
                    walk.dir = (move.x > 0) ? Dir::Right : Dir::Left;
                }
                else {
                    walk.dir = (move.y > 0) ? Dir::Down : Dir::Up;
                }
            }

            // movement + collision
            sf::Vector2f next = p.pos + move * p.speed * dt;
            sf::FloatRect pRect({ next.x, next.y }, { p.size.x, p.size.y });

            bool blocked = false;
            for (auto& w : walls) {
                if (intersects(pRect, w)) { blocked = true; break; }
            }
            if (!blocked) p.pos = next;

            // animate frames
            if (walk.moving) {
                walk.timer += dt;
                if (walk.timer >= walk.frameTime) {
                    walk.timer = 0.f;
                    walk.frame = (walk.frame + 1) % 4;
                }
            }
            else {
                walk.frame = 0;
                walk.timer = 0.f;
            }
            setPlayerFrame();

            if (encounter.active) {
                sf::FloatRect current({ p.pos.x, p.pos.y }, { p.size.x, p.size.y });
                if (intersects(current, encounter.trigger) && justPressed(sf::Keyboard::Key::E, prevE)) {
                    mode = GameMode::EncounterMenu;
                    menuIndex = 0;
                }
            }
        }
        else if (mode == GameMode::EncounterMenu) {
            if (justPressed(sf::Keyboard::Key::W, prevW)) menuIndex = (menuIndex - 1 + 2) % 2;
            if (justPressed(sf::Keyboard::Key::S, prevS)) menuIndex = (menuIndex + 1) % 2;

            if (justPressed(sf::Keyboard::Key::Escape, prevEsc)) {
                mode = GameMode::Overworld;
            }

            if (justPressed(sf::Keyboard::Key::Enter, prevEnter)) {
                if (menuIndex == 0) {
                    mode = GameMode::Overworld;
                }
                else {
                    enemyHp = enemyMaxHp;
                    battleStage = 1;

                    enemyHpShown = (float)enemyHp;
                    enemyHpFrom = (float)enemyHp;
                    enemyHpTo = (float)enemyHp;
                    hpAnimT = 0.f;

                    soul.hp = soul.maxHp;
                    soul.invuln = false;
                    soul.invulnTimer = 0.f;

                    // IMPORTANT: start fly-in instead of instantly going to battle center
                    startSoulFlyIn();
                }
            }
        }
        else if (mode == GameMode::SoulFlyIn) {
            soulFlyT += dt;
            float t01 = (soulFlyDur > 0.f) ? (soulFlyT / soulFlyDur) : 1.f;
            t01 = clampf(t01, 0.f, 1.f);

            // smoothstep easing
            float eased = t01 * t01 * (3.f - 2.f * t01);

            soul.pos = soulFlyStart + (soulFlyTarget - soulFlyStart) * eased;

            if (t01 >= 1.f) {
                soul.pos = soulFlyTarget;
                startBattlePhase();
            }
        }
        else if (mode == GameMode::Battle) {
            battleTime += dt;

            sf::Vector2f move(0.f, 0.f);
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) move.y -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) move.y += 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) move.x -= 1.f;
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) move.x += 1.f;

            if (move.x != 0.f || move.y != 0.f) {
                float len = sqrt(move.x * move.x + move.y * move.y);
                move /= len;
            }

            soul.pos += move * soul.speed * dt;

            soul.pos.x = clampf(soul.pos.x, leftOf(battleBox), rightOf(battleBox) - soul.size.x);
            soul.pos.y = clampf(soul.pos.y, topOf(battleBox), bottomOf(battleBox) - soul.size.y);

            spawnTimer += dt;

            if (battleStage == 1) {
                if (spawnTimer >= 0.25f) {
                    spawnTimer = 0.f;

                    Bullet b;
                    float minX = leftOf(battleBox) + 12.f;
                    float maxX = rightOf(battleBox) - 12.f;
                    float x = minX + rand() % (int)(maxX - minX + 1.f);

                    b.pos = { x, topOf(battleBox) - 10.f };
                    b.vel = { 0.f, 260.f + (float)(rand() % 140) };
                    b.r = 6.f;
                    bullets.push_back(b);
                }
            }
            else {
                if (spawnTimer >= 0.18f) {
                    spawnTimer = 0.f;

                    for (int i = 0; i < 2; i++) {
                        Bullet b;
                        float minX = leftOf(battleBox) + 12.f;
                        float maxX = rightOf(battleBox) - 12.f;
                        float x = minX + rand() % (int)(maxX - minX + 1.f);

                        b.pos = { x, topOf(battleBox) - 10.f };
                        b.vel = { 0.f, 320.f + (float)(rand() % 180) };
                        b.r = 6.f;
                        bullets.push_back(b);
                    }
                }
            }

            for (auto& b : bullets) {
                b.update(dt);
                if (b.pos.y > bottomOf(battleBox) + 40.f) b.alive = false;
            }
            bullets.erase(remove_if(bullets.begin(), bullets.end(),
                [](const Bullet& b) { return !b.alive; }),
                bullets.end());

            if (soul.invuln) {
                soul.invulnTimer -= dt;
                if (soul.invulnTimer <= 0.f) {
                    soul.invuln = false;
                    soul.invulnTimer = 0.f;
                }
            }

            if (!soul.invuln) {
                sf::FloatRect sr({ soul.pos.x, soul.pos.y }, soul.size);
                for (auto& b : bullets) {
                    sf::FloatRect br({ b.pos.x - b.r, b.pos.y - b.r }, { b.r * 2.f, b.r * 2.f });
                    if (intersects(sr, br)) {
                        soul.hp -= 5;
                        soul.invuln = true;
                        soul.invulnTimer = 0.6f;
                        break;
                    }
                }
            }

            if (battleTime >= 12.f) {
                mode = GameMode::AttackTurn;
                bullets.clear();
            }

            if (soul.hp <= 0) {
                mode = GameMode::GameOver;
            }
        }
        else if (mode == GameMode::AttackTurn) {
            if (justPressed(sf::Keyboard::Key::Enter, prevEnter)) {
                lastDamage = 70;
                enemyHp -= lastDamage;
                enemyHp = max(0, enemyHp);

                enemyHpFrom = enemyHpShown;
                enemyHpTo = (float)enemyHp;
                hpAnimT = 0.f;

                if (enemyHp <= 0) {
                    mode = GameMode::EnemyDefeated;
                    defeatTimer = 0.f;
                    encounter.active = false;
                }
                else {
                    mode = GameMode::DamageMsg;
                    msgTimer = 0.f;
                    playedHpDownSfx = false;
                }
            }

            if (justPressed(sf::Keyboard::Key::Escape, prevEsc)) {
                mode = GameMode::Overworld;
            }
        }
        else if (mode == GameMode::DamageMsg) {
            if (!playedHpDownSfx) {
                hpDownSfx.play();
                playedHpDownSfx = true;
            }

            msgTimer += dt;

            hpAnimT += dt;
            float tt = hpAnimDur > 0.f ? (hpAnimT / hpAnimDur) : 1.f;
            tt = clampf(tt, 0.f, 1.f);

            float eased = tt * tt * (3.f - 2.f * tt);
            enemyHpShown = enemyHpFrom + (enemyHpTo - enemyHpFrom) * eased;

            if (tt >= 1.f || justPressed(sf::Keyboard::Key::Enter, prevEnter)) {
                enemyHpShown = enemyHpTo;
                battleStage = 2;

                // Start next defense normally in center
                soul.pos = {
                    leftOf(battleBox) + battleBox.size.x / 2.f - soul.size.x / 2.f,
                    topOf(battleBox) + battleBox.size.y / 2.f - soul.size.y / 2.f
                };
                startBattlePhase();
            }
        }
        else if (mode == GameMode::EnemyDefeated) {
            defeatTimer += dt;
            if (defeatTimer >= 1.5f || justPressed(sf::Keyboard::Key::Enter, prevEnter)) {
                mode = GameMode::Victory;
            }
        }
        else if (mode == GameMode::GameOver) {
            if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::R)) {
                mode = GameMode::Overworld;
                encounter.active = true;
                p.pos = { 120.f, 260.f };
            }
        }
        else if (mode == GameMode::Victory) {
            if (justPressed(sf::Keyboard::Key::Enter, prevEnter)) {
                mode = GameMode::Overworld;
            }
        }

        // -----------------------------
        // DRAW
        // -----------------------------
        window.clear(sf::Color(10, 10, 12));
        window.draw(roomBg);

        auto drawPlayerCenteredOnHitbox = [&]() {
            playerSprite.setPosition({
                p.pos.x + p.size.x / 2.f,
                p.pos.y + p.size.y / 2.f
                });
            window.draw(playerSprite);
            };

        auto drawSoulCenteredOnHitbox = [&]() {
            soulShape.setPosition({
                soul.pos.x + soul.size.x / 2.f,
                soul.pos.y + soul.size.y / 2.f
                });
            window.draw(soulShape);
            };

        if (mode == GameMode::Overworld) {
            for (auto& w : walls) {
                wallShape.setPosition(w.position);
                wallShape.setSize(w.size);
                window.draw(wallShape);
            }

            drawPlayerCenteredOnHitbox();

            if (encounter.active) {
                triggerOutline.setPosition(encounter.trigger.position);
                triggerOutline.setSize(encounter.trigger.size);
                window.draw(triggerOutline);
                drawEnemyAtTrigger();
            }
        }
        else if (mode == GameMode::EncounterMenu) {
            for (auto& w : walls) {
                wallShape.setPosition(w.position);
                wallShape.setSize(w.size);
                window.draw(wallShape);
            }

            drawPlayerCenteredOnHitbox();

            if (encounter.active) {
                triggerOutline.setPosition(encounter.trigger.position);
                triggerOutline.setSize(encounter.trigger.size);
                window.draw(triggerOutline);
                drawEnemyAtTrigger();
            }

            window.draw(menuPanel);

            sf::Vector2f base = menuPanel.getPosition();
            sf::Vector2f opt1 = { base.x + 60.f, base.y + 90.f };
            sf::Vector2f opt2 = { base.x + 60.f, base.y + 135.f };

            sf::Vector2f selPos = (menuIndex == 0)
                ? sf::Vector2f{ base.x + 35.f, base.y + 98.f }
            : sf::Vector2f{ base.x + 35.f, base.y + 143.f };

            selector.setPosition(selPos);
            window.draw(selector);

            if (hasFont) {
                menuTitle.setPosition({ base.x + 40.f, base.y + 30.f });
                optionWalk.setPosition(opt1);
                optionAttack.setPosition(opt2);
                hintText.setPosition({ base.x + 40.f, base.y + 175.f });

                optionWalk.setFillColor(menuIndex == 0 ? sf::Color::Yellow : sf::Color::White);
                optionAttack.setFillColor(menuIndex == 1 ? sf::Color::Yellow : sf::Color::White);

                window.draw(menuTitle);
                window.draw(optionWalk);
                window.draw(optionAttack);
                window.draw(hintText);
            }
        }
        else if (mode == GameMode::SoulFlyIn) {
            // show overworld while heart flies in (looks like Undertale transition)
            for (auto& w : walls) {
                wallShape.setPosition(w.position);
                wallShape.setSize(w.size);
                window.draw(wallShape);
            }

            if (encounter.active) {
                triggerOutline.setPosition(encounter.trigger.position);
                triggerOutline.setSize(encounter.trigger.size);
                window.draw(triggerOutline);
                drawEnemyAtTrigger();
            }

            // draw the battle box outline so you see the target
            window.draw(boxShape);

            // draw heart flying
            drawSoulCenteredOnHitbox();
        }
        else if (mode == GameMode::Battle) {
            window.draw(boxShape);

            for (auto& b : bullets) {
                sf::CircleShape c(b.r);
                c.setFillColor(sf::Color::White);
                c.setPosition(sf::Vector2f{ b.pos.x - b.r, b.pos.y - b.r });
                window.draw(c);
            }

            // blink during invuln
            if (!soul.invuln || fmod(battleTime * 10.f, 2.f) < 1.f) {
                drawSoulCenteredOnHitbox();
            }

            float ratio = (float)max(0, soul.hp) / (float)soul.maxHp;
            hpFill.setSize(sf::Vector2f{ 240.f * ratio, 16.f });
            window.draw(hpBack);
            window.draw(hpFill);

            // enemy sprite above battle box
            enemySprite.setPosition(sf::Vector2f{ leftOf(battleBox) + battleBox.size.x / 2.f, topOf(battleBox) - 90.f });
            window.draw(enemySprite);

            float eratio = (float)std::max(0, enemyHp) / (float)enemyMaxHp;

            enemyHpBack.setPosition({
                leftOf(battleBox) + battleBox.size.x / 2.f - 130.f,
                topOf(battleBox) - 25.f
                });

            enemyHpFill.setPosition(enemyHpBack.getPosition());
            enemyHpFill.setSize({ 260.f * eratio, 12.f });

            window.draw(enemyHpBack);
            window.draw(enemyHpFill);
        }
        else if (mode == GameMode::AttackTurn) {
            sf::RectangleShape overlay(sf::Vector2f((float)W, (float)H));
            overlay.setFillColor(sf::Color(0, 0, 0, 160));
            window.draw(overlay);

            if (hasFont) {
                sf::Text t(font);
                t.setCharacterSize(28);
                t.setFillColor(sf::Color::White);
                t.setString("YOUR TURN!\nPress Enter to attack\nEsc to run");

                auto b = t.getLocalBounds();
                t.setPosition({ W / 2.f - b.size.x / 2.f, H / 2.f - 70.f });
                window.draw(t);

                sf::Text hpText(font);
                hpText.setCharacterSize(18);
                hpText.setFillColor(sf::Color(200, 200, 200));
                hpText.setString("Enemy HP: " + std::to_string(enemyHp) + "/" + std::to_string(enemyMaxHp));
                auto hb = hpText.getLocalBounds();
                hpText.setPosition({ W / 2.f - hb.size.x / 2.f, H / 2.f + 40.f });
                window.draw(hpText);
            }
        }
        else if (mode == GameMode::DamageMsg) {
            sf::RectangleShape overlay(sf::Vector2f((float)W, (float)H));
            overlay.setFillColor(sf::Color(0, 0, 0, 200));
            window.draw(overlay);

            float eratio = (float)std::max(0.f, enemyHpShown) / (float)enemyMaxHp;

            enemyHpBack.setPosition({ W / 2.f - 130.f, H / 2.f - 10.f });
            enemyHpFill.setPosition(enemyHpBack.getPosition());
            enemyHpFill.setSize({ 260.f * eratio, 12.f });

            window.draw(enemyHpBack);
            window.draw(enemyHpFill);

            if (hasFont) {
                sf::Text t(font);
                t.setCharacterSize(28);
                t.setFillColor(sf::Color::White);
                t.setString("YOU DID " + std::to_string(lastDamage) + " DAMAGE!\nHE IS ANGRY NOW");

                auto b = t.getLocalBounds();
                t.setPosition({ W / 2.f - b.size.x / 2.f, H / 2.f - 80.f });
                window.draw(t);

                sf::Text hint(font);
                hint.setCharacterSize(16);
                hint.setFillColor(sf::Color(200, 200, 200));
                hint.setString("Press Enter to continue");

                auto hb = hint.getLocalBounds();
                hint.setPosition({ W / 2.f - hb.size.x / 2.f, H / 2.f + 40.f });
                window.draw(hint);
            }
        }
        else if (mode == GameMode::EnemyDefeated) {
            sf::RectangleShape overlay(sf::Vector2f((float)W, (float)H));
            overlay.setFillColor(sf::Color(0, 0, 0, 210));
            window.draw(overlay);

            if (hasFont) {
                sf::Text t(font);
                t.setCharacterSize(42);
                t.setFillColor(sf::Color::White);
                t.setStyle(sf::Text::Bold);
                t.setString("ENEMY DEFEATED!");

                auto b = t.getLocalBounds();
                t.setPosition({ W / 2.f - b.size.x / 2.f, H / 2.f - 40.f });
                window.draw(t);

                sf::Text h(font);
                h.setCharacterSize(18);
                h.setFillColor(sf::Color(200, 200, 200));
                h.setString("Press Enter to continue");
                auto hb = h.getLocalBounds();
                h.setPosition({ W / 2.f - hb.size.x / 2.f, H / 2.f + 30.f });
                window.draw(h);
            }
        }
        else if (mode == GameMode::Victory) {
            sf::RectangleShape overlay(sf::Vector2f((float)W, (float)H));
            overlay.setFillColor(sf::Color(0, 0, 0, 200));
            window.draw(overlay);

            if (hasFont) {
                auto b1 = victoryTitle.getLocalBounds();
                auto b2 = victoryHint.getLocalBounds();

                victoryTitle.setPosition({ W / 2.f - b1.size.x / 2.f, H / 2.f - 70.f });
                victoryHint.setPosition({ W / 2.f - b2.size.x / 2.f, H / 2.f + 10.f });

                window.draw(victoryTitle);
                window.draw(victoryHint);
            }
        }
        else { // GameOver
            sf::RectangleShape overlay(sf::Vector2f((float)W, (float)H));
            overlay.setFillColor(sf::Color(0, 0, 0, 180));
            window.draw(overlay);

            if (hasFont) {
                sf::Text t(font);
                t.setCharacterSize(32);
                t.setFillColor(sf::Color::Red);
                t.setString("GAME OVER\nPress R to restart");
                auto b = t.getLocalBounds();
                t.setPosition({ W / 2.f - b.size.x / 2.f, H / 2.f - 60.f });
                window.draw(t);
            }
        }

        window.display();
    }

    return 0;
}
