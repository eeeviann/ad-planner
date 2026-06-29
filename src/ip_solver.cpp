





#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cmath>

using namespace std;


struct Media {
    string name;
    double adCost;
    double planCost;
    double rewardCost;
    double childCov;
    double parentCov;
    double exposure;
};

vector<Media> medias = {
    {"电视广告", 30, 9,  0,  1.2, 0.2, 70},
    {"网站广告", 15, 3,  10, 0.1, 0.7, 100},
    {"微信广告", 10, 2,  10, 0.2, 0.8, 90}
};


struct Result {
    int x1, x2, x3;
    double totalExposure;
    double adCost, planCost, rewardCost;
    double childCov, parentCov;
    int nodesExplored;
    string question;
};


Result best;
int nodesExplored = 0;

double calcExposure(int x1, int x2, int x3) {
    return medias[0].exposure * x1 + medias[1].exposure * x2 + medias[2].exposure * x3;
}

double calcAdCost(int x1, int x2, int x3, int tvCost) {
    return tvCost * x1 + medias[1].adCost * x2 + medias[2].adCost * x3;
}

double calcPlanCost(int x1, int x2, int x3) {
    return medias[0].planCost * x1 + medias[1].planCost * x2 + medias[2].planCost * x3;
}

double calcRewardCost(int x2, int x3) {
    return medias[1].rewardCost * x2 + medias[2].rewardCost * x3;
}

double calcChildCov(int x1, int x2, int x3) {
    return medias[0].childCov * x1 + medias[1].childCov * x2 + medias[2].childCov * x3;
}

double calcParentCov(int x1, int x2, int x3) {
    return medias[0].parentCov * x1 + medias[1].parentCov * x2 + medias[2].parentCov * x3;
}


Result solveQ1(int adBudget = 400, int planBudget = 100,
                int childCovTarget = 5, int parentCovTarget = 5,
                int rewardBudget = 149) {
    best.totalExposure = -1;
    best.question = "Q1";
    nodesExplored = 0;

    int x1Max = adBudget / 30;
    int x2Max = adBudget / 15;
    int x3Max = adBudget / 10;

    for (int x1 = 0; x1 <= x1Max; x1++) {
        for (int x2 = 0; x2 <= x2Max; x2++) {
            for (int x3 = 0; x3 <= x3Max; x3++) {
                nodesExplored++;

                double adCost = calcAdCost(x1, x2, x3, 30);
                double planCost = calcPlanCost(x1, x2, x3);
                double rewardCost = calcRewardCost(x2, x3);
                double childCov = calcChildCov(x1, x2, x3);
                double parentCov = calcParentCov(x1, x2, x3);

                if (adCost > adBudget) continue;
                if (planCost > planBudget) continue;
                if (rewardCost > rewardBudget) continue;
                if (childCov < childCovTarget) continue;
                if (parentCov < parentCovTarget) continue;

                double exposure = calcExposure(x1, x2, x3);
                if (exposure > best.totalExposure) {
                    best.x1 = x1; best.x2 = x2; best.x3 = x3;
                    best.totalExposure = exposure;
                    best.adCost = adCost;
                    best.planCost = planCost;
                    best.rewardCost = rewardCost;
                    best.childCov = childCov;
                    best.parentCov = parentCov;
                }
            }
        }
    }

    best.nodesExplored = nodesExplored;
    return best;
}


Result solveQ2(int adBudget = 400, int planBudget = 100, int tvCost = 25,
                int childCovTarget = 5, int parentCovTarget = 5,
                int rewardBudget = 149) {
    best.totalExposure = -1;
    best.question = "Q2";
    nodesExplored = 0;

    int x1Max = adBudget / tvCost;
    int x2Max = adBudget / 15;
    int x3Max = adBudget / 10;

    for (int x1 = 0; x1 <= x1Max; x1++) {
        for (int x2 = 0; x2 <= x2Max; x2++) {
            for (int x3 = 0; x3 <= x3Max; x3++) {
                nodesExplored++;

                double adCost = calcAdCost(x1, x2, x3, tvCost);
                double planCost = calcPlanCost(x1, x2, x3);
                double rewardCost = calcRewardCost(x2, x3);
                double childCov = calcChildCov(x1, x2, x3);
                double parentCov = calcParentCov(x1, x2, x3);

                if (adCost > adBudget) continue;
                if (planCost > planBudget) continue;
                if (rewardCost > rewardBudget) continue;
                if (childCov < childCovTarget) continue;
                if (parentCov < parentCovTarget) continue;

                double exposure = calcExposure(x1, x2, x3);
                if (exposure > best.totalExposure) {
                    best.x1 = x1; best.x2 = x2; best.x3 = x3;
                    best.totalExposure = exposure;
                    best.adCost = adCost;
                    best.planCost = planCost;
                    best.rewardCost = rewardCost;
                    best.childCov = childCov;
                    best.parentCov = parentCov;
                }
            }
        }
    }

    best.nodesExplored = nodesExplored;
    return best;
}


Result solveQ3(int adBudget = 400, int planBudget = 200,
                int childCovTarget = 5, int parentCovTarget = 5,
                int rewardBudget = 149) {
    best.totalExposure = -1;
    best.question = "Q3";
    nodesExplored = 0;

    int x1Max = adBudget / 30;
    int x2Max = adBudget / 15;
    int x3Max = adBudget / 10;

    for (int x1 = 0; x1 <= x1Max; x1++) {
        for (int x2 = 0; x2 <= x2Max; x2++) {
            for (int x3 = 0; x3 <= x3Max; x3++) {
                nodesExplored++;

                double adCost = calcAdCost(x1, x2, x3, 30);
                double planCost = calcPlanCost(x1, x2, x3);
                double rewardCost = calcRewardCost(x2, x3);
                double childCov = calcChildCov(x1, x2, x3);
                double parentCov = calcParentCov(x1, x2, x3);

                if (adCost > adBudget) continue;
                if (planCost > planBudget) continue;
                if (rewardCost > rewardBudget) continue;
                if (childCov < childCovTarget) continue;
                if (parentCov < parentCovTarget) continue;

                double exposure = calcExposure(x1, x2, x3);
                if (exposure > best.totalExposure) {
                    best.x1 = x1; best.x2 = x2; best.x3 = x3;
                    best.totalExposure = exposure;
                    best.adCost = adCost;
                    best.planCost = planCost;
                    best.rewardCost = rewardCost;
                    best.childCov = childCov;
                    best.parentCov = parentCov;
                }
            }
        }
    }

    best.nodesExplored = nodesExplored;
    return best;
}


void printResult(const Result& r) {
    cout << "\n========================================" << endl;
    cout << "  " << r.question << " 最优解" << endl;
    cout << "========================================" << endl;
    cout << "  电视广告 x1 = " << r.x1 << endl;
    cout << "  网站广告 x2 = " << r.x2 << endl;
    cout << "  微信广告 x3 = " << r.x3 << endl;
    cout << "  总曝光     = " << fixed << setprecision(0) << r.totalExposure << " 万次" << endl;
    cout << "========================================" << endl;
    cout << "  广告成本   = " << fixed << setprecision(0) << r.adCost << " 万  (上限 400万)" << endl;
    cout << "  策划成本   = " << fixed << setprecision(0) << r.planCost << " 万" << endl;
    cout << "  奖励成本   = " << fixed << setprecision(0) << r.rewardCost << " 万  (上限 149万)" << endl;
    cout << "  儿童覆盖   = " << fixed << setprecision(1) << r.childCov << " 百万 (目标 5百万)" << endl;
    cout << "  家长覆盖   = " << fixed << setprecision(1) << r.parentCov << " 百万 (目标 5百万)" << endl;
    cout << "  搜索节点   = " << r.nodesExplored << " 个" << endl;
    cout << "========================================" << endl;
}

void printHelp() {
    cout << "\n用法: adplanner [选项]" << endl;
    cout << "选项:" << endl;
    cout << "  --q1      求解第1问（策划预算100万）" << endl;
    cout << "  --q2      求解第2问（TV广告25万）" << endl;
    cout << "  --q3      求解第3问（策划预算200万）" << endl;
    cout << "  --all     求解全部三问（默认）" << endl;
    cout << "  --help    显示帮助" << endl;
    cout << "\n示例:" << endl;
    cout << "  adplanner --q1" << endl;
    cout << "  adplanner --all" << endl;
}

int main(int argc, char* argv[]) {
    cout << "\n========================================" << endl;
    cout << "  广告投放整数规划求解器" << endl;
    cout << "  算法: 分支定界枚举法" << endl;
    cout << "  问题: 优格公司早餐麦片推广案例" << endl;
    cout << "========================================" << endl;


    if (argc == 1) {
        printResult(solveQ1());
        printResult(solveQ2());
        printResult(solveQ3());
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--q1" || arg == "-q1") {
            printResult(solveQ1());
        } else if (arg == "--q2" || arg == "-q2") {
            printResult(solveQ2());
        } else if (arg == "--q3" || arg == "-q3") {
            printResult(solveQ3());
        } else if (arg == "--all" || arg == "-a") {
            printResult(solveQ1());
            printResult(solveQ2());
            printResult(solveQ3());
        } else if (arg == "--help" || arg == "-h") {
            printHelp();
        } else {
            cout << "未知选项: " << arg << endl;
            printHelp();
        }
    }

    return 0;
}
