#ifndef LOGIC_H
#define LOGIC_H

// Declarations for our logic function
void update_gauge_logic(float v, bool night_mode);
void update_engine_status(int temp, float avg_fuel, int speed, float lph, bool is_night_mode);

// Simple Running Average Helper
struct RollingAverage {
    static const int SIZE = 5;
    float samples[SIZE];
    int index = 0;
    int count = 0;
    float sum = 0.0f;

    RollingAverage() {
        for(int i=0; i<SIZE; i++) samples[i] = 0.0f;
    }

    void add(float val) {
        sum -= samples[index];
        samples[index] = val;
        sum += val;
        
        index++;
        if (index >= SIZE) index = 0;
        if (count < SIZE) count++;
    }

    float getAverage() {
        if (count == 0) return 0.0f;
        return sum / count;
    }
};

#endif