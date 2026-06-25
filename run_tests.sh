cd build
echo "======= Inizio run Standard ======="
for i in {1..5}; do
    ./start standard || true
    sleep 1
done

echo "======= Inizio run Overload ======="
for i in {1..5}; do
    ./start overload || true
    sleep 1
done

echo "Fine dei test"

cd ..