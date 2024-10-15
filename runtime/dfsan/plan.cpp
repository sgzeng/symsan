// plan.cpp
#include "plan.h"
#include <cctype>

// ----------------------
// SimplePlan Implementation
// ----------------------
SimplePlan::SimplePlan(const std::string& s, int r)
    : PlanBase(r), step(s) {}

std::unique_ptr<PlanBase> SimplePlan::copy() const {
    return std::make_unique<SimplePlan>(*this);
}

std::vector<std::string> SimplePlan::unfold() const {
    return std::vector<std::string>(repeats, step);
}

std::string SimplePlan::serialize() const {
    std::ostringstream oss;
    if (!step.empty() && step.front() == '(') {
        // Step is a tuple, serialize without quotes
        oss << "(" << step << "," << repeats << ")";
    } else {
        // Step is a string, serialize with quotes
        oss << "(\"" << step << "\"," << repeats << ")";
    }
    return oss.str();
}

// ----------------------
// CompositePlan Implementation
// ----------------------
CompositePlan::CompositePlan(const std::vector<std::shared_ptr<PlanBase>>& sp, int r)
    : PlanBase(r), subplans(sp) {
    for(const auto& sub : subplans) {
        if(!sub) throw std::invalid_argument("Subplan cannot be null");
    }
}

std::unique_ptr<PlanBase> CompositePlan::copy() const {
    std::vector<std::shared_ptr<PlanBase>> copied_subplans;
    for(const auto& sub : subplans) {
        copied_subplans.emplace_back(sub->copy());
    }
    return std::make_unique<CompositePlan>(copied_subplans, repeats);
}

std::vector<std::string> CompositePlan::unfold() const {
    std::vector<std::string> result;
    for(int i = 0; i < repeats; ++i) {
        for(const auto& sub : subplans) {
            std::vector<std::string> sub_unfold = sub->unfold();
            result.insert(result.end(), sub_unfold.begin(), sub_unfold.end());
        }
    }
    return result;
}

std::string CompositePlan::serialize() const {
    std::ostringstream oss;
    oss << "([";
    for(size_t i = 0; i < subplans.size(); ++i) {
        oss << subplans[i]->serialize();
        if(i != subplans.size() - 1) oss << ",";
    }
    oss << "]," << repeats << ")";
    return oss.str();
}

// ----------------------
// Plan Implementation
// ----------------------

// Private constructor for deserialization
Plan::Plan(std::shared_ptr<PlanBase> p) : plan_ptr(p) {}

// Default constructor initializes an empty plan
Plan::Plan() : plan_ptr(nullptr) {}

// Constructor for a simple plan
Plan::Plan(const std::string& step, int r) {
    if(!step.empty()) {
        plan_ptr = std::make_shared<SimplePlan>(step, r);
    } else {
        plan_ptr = nullptr;
    }
}

// Constructor for a composite plan
Plan::Plan(const std::vector<Plan>& subplans, int r) {
    std::vector<std::shared_ptr<PlanBase>> sp;
    for(const auto& p : subplans) {
        sp.emplace_back(p.plan_ptr);
    }
    plan_ptr = std::make_shared<CompositePlan>(sp, r);
}

// Merges the current plan with another plan
Plan Plan::merge(const Plan& other) const {
    std::vector<Plan> new_subplans = { *this, other.copy() };
    return Plan(new_subplans, 1);
}

// Creates a deep copy of the plan
Plan Plan::copy() const {
    if(plan_ptr) {
        Plan new_plan("");
        new_plan.plan_ptr = plan_ptr->copy();
        return new_plan;
    } else {
        return Plan();
    }
}

// Unfolds the plan into a list of steps
std::vector<std::string> Plan::unfold() const {
    if(plan_ptr) {
        return plan_ptr->unfold();
    } else {
        return {};
    }
}

// Serializes the plan into a string
std::string Plan::serialize() const {
    if(plan_ptr) {
        return plan_ptr->serialize();
    } else {
        return "";
    }
}

std::pair<int, int> Plan::parse_tuple(const std::string& s) {
    size_t pos = 0;

    // Helper lambda to skip whitespace
    auto skip_whitespace = [&](size_t& pos_ref) {
        while (pos_ref < s.size() && isspace(s[pos_ref])) pos_ref++;
    };

    skip_whitespace(pos);

    if (pos >= s.size() || s[pos] != '(')
        throw std::invalid_argument("Expected '(' at the beginning of tuple");

    pos++; // Skip '('
    skip_whitespace(pos);

    // Parse the first number
    size_t start_num1 = pos;
    while (pos < s.size() && (isdigit(s[pos]) || s[pos] == '+' || s[pos] == '-')) pos++;
    if (start_num1 == pos)
        throw std::invalid_argument("Expected first integer in tuple");

    int num1 = std::stoi(s.substr(start_num1, pos - start_num1));

    skip_whitespace(pos);

    if (pos >= s.size() || s[pos] != ',')
        throw std::invalid_argument("Expected ',' in tuple");

    pos++; // Skip ','
    skip_whitespace(pos);

    // Parse the second number
    size_t start_num2 = pos;
    while (pos < s.size() && (isdigit(s[pos]) || s[pos] == '+' || s[pos] == '-')) pos++;
    if (start_num2 == pos)
        throw std::invalid_argument("Expected second integer in tuple");

    int num2 = std::stoi(s.substr(start_num2, pos - start_num2));

    skip_whitespace(pos);

    if (pos >= s.size() || s[pos] != ')')
        throw std::invalid_argument("Expected ')' at the end of tuple");

    // pos++; // Skip ')', not needed here

    return std::make_pair(num1, num2);
}

// Deserializes a string into a Plan object
Plan Plan::deserialize(const std::string& s) {
    size_t pos = 0;

    // Helper lambda to skip whitespace
    auto skip_whitespace = [&](size_t& pos_ref) {
        while(pos_ref < s.size() && isspace(s[pos_ref])) pos_ref++;
    };

    // Recursive lambda function to parse a Plan
    std::function<std::shared_ptr<PlanBase>(const std::string&, size_t&)> parse_plan = [&](const std::string& str, size_t& pos_ref) -> std::shared_ptr<PlanBase> {
        skip_whitespace(pos_ref);
        if(pos_ref >= str.size()) throw std::invalid_argument("Unexpected end of string");

        if(str[pos_ref] != '(') throw std::invalid_argument("Expected '(' at the beginning of a plan");

        pos_ref++; // skip '('
        skip_whitespace(pos_ref);

        if(str[pos_ref] == '[') {
            // CompositePlan
            pos_ref++; // skip '['
            std::vector<std::shared_ptr<PlanBase>> subplans;
            while(str[pos_ref] != ']') {
                subplans.emplace_back(parse_plan(str, pos_ref));
                skip_whitespace(pos_ref);
                if(str[pos_ref] == ',') {
                    pos_ref++; // skip ','
                    skip_whitespace(pos_ref);
                }
            }
            pos_ref++; // skip ']'
            skip_whitespace(pos_ref);
            if(str[pos_ref] != ',') throw std::invalid_argument("Expected ',' after subplans");
            pos_ref++; // skip ','
            skip_whitespace(pos_ref);

            // Parse repeats
            size_t start = pos_ref;
            while(pos_ref < str.size() && isdigit(str[pos_ref])) pos_ref++;
            if(start == pos_ref) throw std::invalid_argument("Expected number for repeats");
            int repeats = std::stoi(str.substr(start, pos_ref - start));
            skip_whitespace(pos_ref);
            if(str[pos_ref] != ')') throw std::invalid_argument("Expected ')' at the end of composite plan");
            pos_ref++; // skip ')'

            return std::make_shared<CompositePlan>(subplans, repeats);
        }
        else if(str[pos_ref] == '\"') {
            // SimplePlan with string step
            pos_ref++; // skip '\"'
            size_t start = pos_ref;
            while(pos_ref < str.size() && str[pos_ref] != '\"') pos_ref++;
            if(pos_ref >= str.size()) throw std::invalid_argument("Unterminated string in simple plan");
            std::string step = str.substr(start, pos_ref - start);
            pos_ref++; // skip '\"'
            skip_whitespace(pos_ref);
            if(str[pos_ref] != ',') throw std::invalid_argument("Expected ',' after step in simple plan");
            pos_ref++; // skip ','
            skip_whitespace(pos_ref);

            // Parse repeats
            size_t start_num = pos_ref;
            while(pos_ref < str.size() && isdigit(str[pos_ref])) pos_ref++;
            if(start_num == pos_ref) throw std::invalid_argument("Expected number for repeats in simple plan");
            int repeats = std::stoi(str.substr(start_num, pos_ref - start_num));
            skip_whitespace(pos_ref);
            if(str[pos_ref] != ')') throw std::invalid_argument("Expected ')' at the end of simple plan");
            pos_ref++; // skip ')'

            return std::make_shared<SimplePlan>(step, repeats);
        }
        else if(str[pos_ref] == '(') {
            // SimplePlan with tuple step
            size_t start = pos_ref;
            int parentheses = 1;
            pos_ref++; // skip '('
            while(pos_ref < str.size() && parentheses > 0) {
                if(str[pos_ref] == '(') parentheses++;
                else if(str[pos_ref] == ')') parentheses--;
                pos_ref++;
            }
            if(parentheses != 0) throw std::invalid_argument("Unbalanced parentheses in tuple step");
            std::string step = str.substr(start, pos_ref - start);
            skip_whitespace(pos_ref);
            if(str[pos_ref] != ',') throw std::invalid_argument("Expected ',' after step in simple plan");
            pos_ref++; // skip ','
            skip_whitespace(pos_ref);

            // Parse repeats
            size_t start_num = pos_ref;
            while(pos_ref < str.size() && isdigit(str[pos_ref])) pos_ref++;
            if(start_num == pos_ref) throw std::invalid_argument("Expected number for repeats in simple plan");
            int repeats = std::stoi(str.substr(start_num, pos_ref - start_num));
            skip_whitespace(pos_ref);
            if(str[pos_ref] != ')') throw std::invalid_argument("Expected ')' at the end of simple plan");
            pos_ref++; // skip ')'

            return std::make_shared<SimplePlan>(step, repeats);
        }
        else {
            throw std::invalid_argument("Invalid format in plan deserialization");
        }
    };

    // Parse the entire string
    std::shared_ptr<PlanBase> root = parse_plan(s, pos);
    skip_whitespace(pos);
    if(pos != s.size()) throw std::invalid_argument("Extra characters after parsing plan");

    // Use the private constructor to create Plan
    return Plan(root);
}

// Prints the unfolded steps to the standard output
void Plan::print_unfolded() const {
    std::vector<std::string> steps = unfold();
    for(const auto& step : steps) {
        std::cout << step << " ";
    }
    std::cout << std::endl;
}