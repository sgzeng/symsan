// plan.h

#ifndef PLAN_H
#define PLAN_H

#include <iostream>
#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <stdexcept>
#include <cassert>

// Abstract base class for all plan types
class PlanBase {
public:
    int repeats;

    PlanBase(int r = 1) : repeats(r) {}
    virtual ~PlanBase() {}

    // Creates a deep copy of the plan
    virtual std::unique_ptr<PlanBase> copy() const = 0;

    // Unfolds the plan into a list of steps
    virtual std::vector<std::string> unfold() const = 0;

    // Serializes the plan into a string
    virtual std::string serialize() const = 0;
};

// Represents a simple step in the plan
class SimplePlan : public PlanBase {
public:
    std::string step; // a tuple represented as a string

    SimplePlan(const std::string& s, int r = 1);
    virtual std::unique_ptr<PlanBase> copy() const override;
    virtual std::vector<std::string> unfold() const override;
    virtual std::string serialize() const override;
};

// Represents a composite plan containing multiple subplans
class CompositePlan : public PlanBase {
public:
    std::vector<std::shared_ptr<PlanBase>> subplans;

    CompositePlan(const std::vector<std::shared_ptr<PlanBase>>& sp, int r = 1);
    virtual std::unique_ptr<PlanBase> copy() const override;
    virtual std::vector<std::string> unfold() const override;
    virtual std::string serialize() const override;
};

// The Plan class acts as a wrapper around PlanBase, providing user-facing functionalities
class Plan {
private:
    std::shared_ptr<PlanBase> plan_ptr;

    // Private constructor for deserialization
    Plan(std::shared_ptr<PlanBase> p); // Add this line

public:
    // Default constructor for an empty plan
    Plan();

    // Constructor for a simple plan
    Plan(const std::string& step, int r = 1);

    // Constructor for a composite plan
    Plan(const std::vector<Plan>& subplans, int r = 1);

    // Merges the current plan with another plan
    Plan merge(const Plan& other) const;

    // Creates a deep copy of the plan
    Plan copy() const;

    // Unfolds the plan into a list of steps
    std::vector<std::string> unfold() const;

    // Serializes the plan into a string
    std::string serialize() const;

    // Deserializes a string into a Plan object
    static Plan deserialize(const std::string& s);

    // Static helper function to parse a tuple string into a std::pair<int, int>
    static std::pair<int, int> parse_tuple(const std::string& s);
    
    // Prints the unfolded steps to the standard output
    void print_unfolded() const;
};

#endif // PLAN_H