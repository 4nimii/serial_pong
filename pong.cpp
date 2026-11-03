#include <SDL2/SDL.h>
#include <iostream>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <string>
#include <vector>


#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define PADDLE_WIDTH 15
#define PADDLE_HEIGHT 100
#define BALL_SIZE 10
#define PADDLE_SPEED 30
#define BALL_SPEED 5.0f
#define MAX_BALL_SPEED 12.0f

#define SERIAL_MIN_VALUE 2.0f    // |
#define SERIAL_MAX_VALUE 100.0f  // |- Shoul prob. be dyn. calc, but that also mean the first few seconds during which the value are corrected would be awkward or unplayable, also, if the "peak" value is reached only twice and then on average the values are a few order of magnitude lower than the max or higher than the min, the game would also become unplayable.
#define SERIAL_DEVICE "/dev/ttyUSB0"
#define SERIAL_BAUD_RATE 115200

#define PLOT_NB_VALUE 100

// THis should maybe be made into its own class. .-.
std::vector<float> plot;


/**
 * @class SerialPort
 * @brief Manage a serial device.
 * @param device The path to the device tty.
 * @param baudrate Bit rate of the device.
 */
class SerialPort {

    private:

        int fd;
        std::string buffer;

    public:

        SerialPort( const std::string& device, int baudrate ) {

            fd = open( device.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK );
            if ( fd < 0 ) {

                std::cerr << "Error opening serial device: " << device << std::endl;
                return;

            }

            struct termios tty;
            if ( tcgetattr( fd, &tty ) != 0 ) {

                std::cerr << "Error getting serial attributes" << std::endl;
                close( fd );
                fd = -1;
                return;

            }

            speed_t speed;
            switch ( baudrate ) {

                case 9600: speed = B9600; break;
                case 19200: speed = B19200; break;
                case 38400: speed = B38400; break;
                case 115200: speed = B115200; break;
                default: speed = B9600;

            }

            cfsetispeed( &tty, speed );
            cfsetospeed( &tty, speed );

            tty.c_cflag &= ~PARENB;
            tty.c_cflag &= ~CSTOPB;
            tty.c_cflag &= ~CSIZE;
            tty.c_cflag |= CS8;
            tty.c_lflag &= ~( ICANON | ECHO );
            tty.c_cc[VMIN] = 0;
            tty.c_cc[VTIME] = 0;

            tcsetattr( fd, TCSANOW, &tty );

        }

        ~SerialPort() {

            if (fd >= 0) {

                close( fd );

            }

        }

        bool isOpen() const { return fd >= 0; }

        bool readFloat( float& value ) {

            if ( fd < 0 ) return false;

            char ch;
            while ( read( fd, &ch, 1 ) > 0 ) {

                if ( ch == '\n' ) {

                    if ( !buffer.empty() ) {

                        try {

                            value = std::stof( buffer );
                            buffer.clear();
                            return true;

                        } catch ( const std::exception& e ) {

                            std::cerr << "Parse error: " << e.what() << std::endl;
                            buffer.clear();
                            return false;

                        }

                    }

                } else if ( ch == '\r' ) {

                    // Pass

                } else {

                    buffer += ch;

                }

            }

            return false;

        }

};


/**
 * @class Paddle
 * @brief Represent a Pong paddle.
 * @param startX Start position on the x axis.
 * @param startY Start position oon the y axis.
 * @param w Width of the paddle.
 * @param h Height of the paddle.
 */
struct Paddle {

    float x, y;
    int width, height;

    Paddle( float startX, float startY, int w, int h )
        : x( startX ), y( startY ), width( w ), height( h ) { }

    void update( float velocity, int screenHeight ) {

        y += velocity;
        if ( y < 0 ) y = 0;
        if ( y + height > screenHeight ) y = screenHeight - height;

    }

    SDL_Rect getRect() const {

        return { ( int )x, ( int )y, width, height };

    }

    bool collidesWith( float ballX, float ballY, int ballSize ) const {

        return ballX + ballSize > x &&
               ballX < x + width &&
               ballY + ballSize > y &&
               ballY < y + height;

    }

};


/**
 * @class Ball
 * @brief A the ball for a Pong game.
 * @param startX Start position on the x axis.
 * @param startY Start position oon the y axis.
 * @param s Size of the ball.
 */
struct Ball {

    float x, y;
    float vx, vy;
    int size;

    Ball( float startX, float startY, int s )
        : x(startX), y( startY ), vx( BALL_SPEED ), vy( BALL_SPEED ), size( s ) { }

    void update() {

        x += vx;
        y += vy;

    }

    void bounceY() { vy = -vy; }

    void bounceX() { vx = -vx; }

    SDL_Rect getRect() const {

        return { ( int )x, ( int )y, size, size };

    }

    bool isOutOfBounds( int screenWidth ) const {

        return x < -size || x > screenWidth;

    }

    void reset(int screenWidth, int screenHeight) {

        x = screenWidth / 2.0f;
        y = screenHeight / 2.0f;
        vx = BALL_SPEED;
        vy = BALL_SPEED;

    }

};


/**
 * @class Robot
 * @brief Contain the logic which control the robot paddle.
 * @param diff Modify the "error" the robot make when calculating its next position.
 *
 */
class Robot {

    private:

        float difficulty;

    public:

        Robot( float diff = 0.2f ) : difficulty( diff ) { }

        float calculateVelocity( const Ball& ball, const Paddle& paddle ) {

            float timeToReachPaddle = ( paddle.x - ball.x ) / ( ball.vx > 0 ? ball.vx : 0.1f );
            if ( timeToReachPaddle < 0 ) timeToReachPaddle = 0;

            float predictedY = ball.y + ball.vy * timeToReachPaddle;

            if ( predictedY < 0 ) predictedY = 0;
            if ( predictedY > SCREEN_HEIGHT ) predictedY = SCREEN_HEIGHT;

            float paddleCenter = paddle.y + paddle.height / 2.0f;

            float error = predictedY - paddleCenter;

            float effectiveError = error * difficulty;

            if ( effectiveError > PADDLE_SPEED ) {
                return PADDLE_SPEED;
            } else if ( effectiveError < -PADDLE_SPEED ) {
                return -PADDLE_SPEED;
            }
            return effectiveError;
        }

};


/**
 * @class Pong
 * @brief Manage the game objects and the SDL window.
 */
class Pong {

    private:

        SDL_Window* window;
        SDL_Renderer* renderer;
        bool running;
        int score1, score2;

        Paddle playerPaddle;
        Paddle robotPaddle;
        Ball ball;
        Robot robot;
        SerialPort serialPort;

public:

    Pong()
        : window( nullptr ), renderer( nullptr ), running( true ),
          score1( 0 ), score2( 0 ),
          playerPaddle( 20, SCREEN_HEIGHT / 2 - PADDLE_HEIGHT / 2, PADDLE_WIDTH, PADDLE_HEIGHT ),
          robotPaddle( SCREEN_WIDTH - 20 - PADDLE_WIDTH, SCREEN_HEIGHT / 2 - PADDLE_HEIGHT / 2,
                      PADDLE_WIDTH, PADDLE_HEIGHT ),
          ball( SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, BALL_SIZE ),
          robot( 0.85f ),
          serialPort( SERIAL_DEVICE, SERIAL_BAUD_RATE ) {

        if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {

            std::cerr << "SDL initialization failed: " << SDL_GetError() << std::endl;
            return;

        }

        window = SDL_CreateWindow(

            "Pong - Player vs Robot",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            SCREEN_WIDTH, SCREEN_HEIGHT,
            SDL_WINDOW_SHOWN

        );

        if ( !window ) {

            std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
            SDL_Quit();
            return;

        }

        renderer = SDL_CreateRenderer(

            window, -1,
            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC

        );

        if ( !renderer ) {

            std::cerr << "Renderer creation failed: " << SDL_GetError() << std::endl;
            SDL_DestroyWindow( window );
            SDL_Quit();
            return;

        }

        if ( serialPort.isOpen() ) {

            std::cout << "Serial port opened" << std::endl;

        } else {

            std::cout << "Serial port not available" << std::endl;

        }
    }

    ~Pong() {

        if ( renderer ) SDL_DestroyRenderer( renderer );
        if ( window ) SDL_DestroyWindow( window );
        SDL_Quit();

    }

    void handleInput() {

        SDL_Event event;
        while (SDL_PollEvent(&event)) {

            if (event.type == SDL_QUIT) {

                running = false;

            } else if (event.type == SDL_KEYDOWN) {

                if (event.key.keysym.sym == SDLK_ESCAPE) {

                    running = false;

                }

            }

        }

        float serialValue;
        if (serialPort.readFloat(serialValue)) {

            plot.push_back( serialValue );
            std::cout << "Lux: " << serialValue << std::endl;
            if ( plot.size() > PLOT_NB_VALUE ) {

                plot.erase(plot.begin() + 1);

            }

            if ( serialValue < SERIAL_MIN_VALUE ) serialValue = SERIAL_MIN_VALUE;
            if ( serialValue > SERIAL_MAX_VALUE ) serialValue = SERIAL_MAX_VALUE;

            float normalizedValue = ( serialValue - SERIAL_MIN_VALUE ) / ( SERIAL_MAX_VALUE - SERIAL_MIN_VALUE );
            normalizedValue = normalizedValue * 2.0f - 1.0f;
            float paddleVelocity = normalizedValue * PADDLE_SPEED;

            playerPaddle.update( paddleVelocity, SCREEN_HEIGHT );

        }

    }

    void update() {

        ball.update();

        if ( ball.y <= 0 || ball.y + BALL_SIZE >= SCREEN_HEIGHT ) {

            ball.bounceY();

        }

        if ( ball.vx < 0 && playerPaddle.collidesWith( ball.x, ball.y, BALL_SIZE ) ) {

            ball.bounceX();
            ball.x = playerPaddle.x + PADDLE_WIDTH;
            float hitPos = ( ball.y - playerPaddle.y ) / PADDLE_HEIGHT;
            ball.vy += ( hitPos - 0.5f ) * 4.0f;

        }

        if ( ball.vx > 0 && robotPaddle.collidesWith( ball.x, ball.y, BALL_SIZE ) ) {

            ball.bounceX();
            ball.x = robotPaddle.x - BALL_SIZE;
            float hitPos = ( ball.y - robotPaddle.y ) / PADDLE_HEIGHT;
            ball.vy += ( hitPos - 0.5f ) * 4.0f;

        }

        float speed = std::sqrt( ball.vx * ball.vx + ball.vy * ball.vy );
        if ( speed > MAX_BALL_SPEED ) {

            ball.vx = ( ball.vx / speed ) * MAX_BALL_SPEED;
            ball.vy = ( ball.vy / speed ) * MAX_BALL_SPEED;

        }

        if ( ball.isOutOfBounds( SCREEN_WIDTH ) ) {

            if (ball.x < 0) {

                score2++;

            } else {

                score1++;

            }
            ball.reset( SCREEN_WIDTH, SCREEN_HEIGHT );

        }

        float robotVelocity = robot.calculateVelocity( ball, robotPaddle );
        robotPaddle.update( robotVelocity, SCREEN_HEIGHT );

    }

    void render() {

        SDL_SetRenderDrawColor( renderer, 0, 0, 0, 255 );
        SDL_RenderClear( renderer );

        SDL_SetRenderDrawColor( renderer, 255, 255, 255, 255 );
        for ( int i = 0; i < SCREEN_HEIGHT; i += 20 ) {

            SDL_RenderDrawLine( renderer, SCREEN_WIDTH / 2, i, SCREEN_WIDTH / 2, i + 10 );

        }

        SDL_Rect playerRect = playerPaddle.getRect();
        SDL_Rect robotRect = robotPaddle.getRect();
        SDL_RenderFillRect( renderer, &playerRect );
        SDL_RenderFillRect( renderer, &robotRect );

        SDL_Rect ballRect = ball.getRect();
        SDL_RenderFillRect( renderer, &ballRect );

        static int lastScore1 = -1, lastScore2 = -1;
        if ( score1 != lastScore1 || score2 != lastScore2 ) {

            lastScore1 = score1;
            lastScore2 = score2;
            std::cout << "Player: " << score1 << ", Robot: " << score2 << std::endl;

        }

        int frame_size = SCREEN_WIDTH / PLOT_NB_VALUE;
        for ( int i = 1; i < plot.size(); i++ ) {

            SDL_SetRenderDrawColor( renderer, 255, 0, 0, 255 );
            SDL_RenderDrawLine( renderer, i * frame_size, ( int )plot[i], ( i - 1 ) * frame_size, ( int )plot[i - 1] );

        }

        SDL_RenderPresent( renderer );
    }

    void run() {

        const int FPS = 60;
        const int FRAME_DELAY = 1000 / FPS;

        while ( running ) {

            uint32_t frameStart = SDL_GetTicks();

            handleInput();
            update();
            render();

            uint32_t frameTime = SDL_GetTicks() - frameStart;
            if ( frameTime < FRAME_DELAY ) {

                SDL_Delay( FRAME_DELAY - frameTime );

            }

        }

    }

    bool isInitialized() const {

        return window != nullptr && renderer != nullptr;

    }

};


int main() {

    Pong pong;

    if ( !pong.isInitialized() ) {

        std::cerr << "Failed to initialize PONG" << std::endl;
        return 1;

    }

    pong.run();

    return 0;

}
