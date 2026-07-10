#ifndef LOGIC_H
#define LOGIC_H

// Declarations for our logic function
void update_gauge_logic(float v, bool value_valid, bool night_mode);
void update_engine_status(int temp, bool temp_valid,
                          float avg_fuel, bool fuel_valid,
                          bool is_night_mode);

// Rolling L/100 km calculated from fuel and distance over the most recent 1 km.
// Fuel used while idling remains in the window, so city stops affect the result.
struct RecentFuelConsumption {
    static const int CHECKPOINT_COUNT = 128;

    struct Checkpoint {
        double distance_km;
        double fuel_l;
    };

    Checkpoint checkpoints[CHECKPOINT_COUNT];
    int next_index;
    int count;
    double total_distance_km;
    double total_fuel_l;
    double next_checkpoint_km;

    RecentFuelConsumption() {
        reset();
    }

    void reset() {
        next_index = 1;
        count = 1;
        total_distance_km = 0.0;
        total_fuel_l = 0.0;
        next_checkpoint_km = 0.01;

        for (int i = 0; i < CHECKPOINT_COUNT; i++) {
            checkpoints[i].distance_km = 0.0;
            checkpoints[i].fuel_l = 0.0;
        }
    }

    void add(double fuel_l, double distance_km) {
        if (fuel_l < 0.0 || distance_km < 0.0) return;

        total_fuel_l += fuel_l;
        total_distance_km += distance_km;

        // Keep a checkpoint every 10 m. 128 entries retain enough history
        // to select a baseline approximately 1 km behind the current position.
        while (total_distance_km >= next_checkpoint_km) {
            checkpoints[next_index].distance_km = total_distance_km;
            checkpoints[next_index].fuel_l = total_fuel_l;
            next_index = (next_index + 1) % CHECKPOINT_COUNT;
            if (count < CHECKPOINT_COUNT) count++;
            next_checkpoint_km += 0.01;
        }
    }

    float getAverageL100km() const {
        // Avoid unstable values immediately after starting the car.
        if (total_distance_km < 0.10) return -1.0f;

        const double target_distance = total_distance_km - 1.0;
        double base_distance = 0.0;
        double base_fuel = 0.0;

        if (target_distance > 0.0) {
            bool found = false;
            double oldest_distance = total_distance_km;
            double oldest_fuel = total_fuel_l;

            for (int i = 0; i < count; i++) {
                int index = next_index - count + i;
                while (index < 0) index += CHECKPOINT_COUNT;
                index %= CHECKPOINT_COUNT;

                const Checkpoint &checkpoint = checkpoints[index];

                if (checkpoint.distance_km < oldest_distance) {
                    oldest_distance = checkpoint.distance_km;
                    oldest_fuel = checkpoint.fuel_l;
                }

                if (checkpoint.distance_km <= target_distance &&
                    (!found || checkpoint.distance_km > base_distance)) {
                    base_distance = checkpoint.distance_km;
                    base_fuel = checkpoint.fuel_l;
                    found = true;
                }
            }

            // Defensive fallback in case the oldest checkpoint is already
            // newer than the desired 1 km baseline.
            if (!found) {
                base_distance = oldest_distance;
                base_fuel = oldest_fuel;
            }
        }

        const double measured_distance = total_distance_km - base_distance;
        if (measured_distance <= 0.0) return -1.0f;

        return (float)(((total_fuel_l - base_fuel) / measured_distance) * 100.0);
    }
};

#endif